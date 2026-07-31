/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/client/grpc_client.h"

#include <chrono>
#include <exception>
#include <vector>

#include "src/common/logging.h"
#include "src/client/authentication_manager.h"
#include "src/client/platform_ca_bundle.h"
#include "src/client/report_manager.h"
#include "src/client/ssl_config_manager.h"
#include "src/impl/config_manager.h"
#include "src/proto/service.grpc.pb.h"
#include "src/util/util.h"

namespace tbox {
namespace client {

std::pair<std::string, bool> GrpcClient::ParseHostname(
    const std::string& hostname) {
  std::string cleaned = hostname;
  bool use_http = false;

  if (cleaned.find("http://") == 0) {
    cleaned = cleaned.substr(7);  // Remove "http://"
    use_http = true;
  } else if (cleaned.find("https://") == 0) {
    cleaned = cleaned.substr(8);  // Remove "https://"
    use_http = false;
  }

  return {cleaned, use_http};
}

GrpcClient::GrpcClient() {
  auto config_manager = util::ConfigManager::Instance();

  // Parse hostname and strip protocol prefix
  auto [hostname, use_http] = ParseHostname(config_manager->ServerAddr());
  target_address_ =
      hostname + ":" + std::to_string(config_manager->GrpcServerPort());

  if (!use_http) {
    PlatformCABundle::Refresh(config_manager->LocalCertPath());
  }

  if (!Init()) {
    LOG(ERROR) << "Failed to initialize gRPC channel";
  }
}

GrpcClient::~GrpcClient() {
  Stop();
}

bool GrpcClient::Init() {
  auto config_manager = util::ConfigManager::Instance();
  std::shared_ptr<grpc::ChannelCredentials> channel_creds;
  grpc::ChannelArguments args;

  // Parse hostname and determine protocol
  std::string server_addr = config_manager->ServerAddr();
  auto [hostname, use_http] = ParseHostname(server_addr);

  if (use_http) {
    // Use insecure channel for HTTP
    channel_creds = grpc::InsecureChannelCredentials();
    LOG(INFO) << "Using insecure gRPC channel (HTTP/2)";
  } else {
    // Load SSL certificate using configuration path
    // Get certificate path from configuration
    std::string ca_cert_path = config_manager->LocalCertPath();
    if (ca_cert_path.empty()) {
      ca_cert_path = "conf/ca-bundle.pem";  // Default fallback
    }

    std::string ca_cert = SSLConfigManager::LoadCACert(ca_cert_path);
    if (ca_cert.empty()) {
      LOG(ERROR) << "Failed to load CA certificate from: " << ca_cert_path;
      return false;
    }

    // Configure SSL
    grpc::SslCredentialsOptions ssl_opts;
    ssl_opts.pem_root_certs = ca_cert;
    channel_creds = grpc::SslCredentials(ssl_opts);

    // Configure SSL-specific arguments - removed override to use native
    // certificate validation std::string ssl_target_name = "xiedeacc.com";
    // args.SetSslTargetNameOverride(ssl_target_name);
    // args.SetString(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG, ssl_target_name);
    LOG(INFO) << "Using secure gRPC channel (HTTPS)";
  }

  // Create channel
  channel_ = grpc::CreateCustomChannel(target_address_, channel_creds, args);
  if (!channel_) {
    LOG(ERROR) << "Failed to create gRPC channel to " << target_address_;
    return false;
  }

  stub_ = tbox::proto::TBOXService::NewStub(channel_);
  if (!stub_) {
    LOG(ERROR) << "Failed to create gRPC stub";
    return false;
  }

  // Wait for connection
  int connection_timeout_seconds = 10;
  int64_t current_millis = util::Util::CurrentTimeMillis();
  auto deadline =
      std::chrono::system_clock::time_point(std::chrono::milliseconds(
          current_millis + (connection_timeout_seconds * 1000)));
  uint32_t grpc_port = config_manager->GrpcServerPort();
  if (channel_->WaitForConnected(deadline)) {
    LOG(INFO) << "Connected to " << server_addr << ":" << grpc_port
              << " successfully";
  } else {
    LOG(WARNING) << "Failed to connect to " << server_addr << ":" << grpc_port
                 << " within " << connection_timeout_seconds
                 << " seconds timeout";
    // Don't return false - we'll retry in the reporting loop
  }

  // Initialize authentication manager singleton
  auto auth_manager = AuthenticationManager::Instance();
  auth_manager->Init(stub_);
  LOG(INFO) << "Authentication manager initialized";

  // Initialize SSL config manager (if certificate updates are enabled)
  if (!use_http) {
    auto ssl_config_manager = SSLConfigManager::Instance();
    ssl_config_manager->Init(channel_);
    LOG(INFO) << "SSL config manager initialized";
  }

  // Initialize report manager singleton
  auto report_manager = ReportManager::Instance();
  uint32_t check_interval_seconds = config_manager->CheckIntervalSeconds();
  int login_retry_seconds = 60;
  if (report_manager->Init(channel_, check_interval_seconds,
                           login_retry_seconds)) {
    LOG(INFO) << "Report manager initialized";
  } else {
    LOG(ERROR) << "Failed to initialize report manager";
  }

  return true;
}

void GrpcClient::Start() {
  std::lock_guard<std::mutex> lock(lifecycle_mutex_);
  StartManagers();

  auto config_manager = util::ConfigManager::Instance();
  auto [hostname, use_http] = ParseHostname(config_manager->ServerAddr());
  (void)hostname;
  if (!use_http && !ca_monitor_thread_.joinable()) {
    ca_monitor_stop_.store(false);
    ca_monitor_thread_ =
        std::thread(&GrpcClient::MonitorPlatformCABundle, this);
    LOG(INFO) << "Platform CA bundle monitor started";
  }
}

void GrpcClient::StartManagers() {
  // Start SSL config manager (if certificate updates are enabled)
  auto ssl_config_manager = SSLConfigManager::Instance();
  if (!ssl_config_manager->IsRunning()) {
    ssl_config_manager->Start();
    LOG(INFO) << "SSL config manager started";
  }

  // Start report manager
  auto report_manager = ReportManager::Instance();
  if (!report_manager->IsRunning()) {
    report_manager->Start();
    LOG(INFO) << "Report manager started";
  }
}

void GrpcClient::Stop() {
  LOG(INFO) << "Stopping GrpcClient...";
  StopPlatformCAMonitor();

  std::lock_guard<std::mutex> lock(lifecycle_mutex_);
  StopManagers();
  LOG(INFO) << "GrpcClient stopped";
}

void GrpcClient::StopManagers() {
  // Stop SSL config manager
  auto ssl_config_manager = SSLConfigManager::Instance();
  ssl_config_manager->Stop();
  LOG(INFO) << "SSL config manager stopped";

  // Stop report manager (always call Stop to ensure cleanup)
  auto report_manager = ReportManager::Instance();
  report_manager->Stop();
  LOG(INFO) << "Report manager stopped";
}

bool GrpcClient::IsRunning() const {
  auto report_manager = ReportManager::Instance();
  return report_manager->IsRunning();
}

void GrpcClient::MonitorPlatformCABundle() {
  auto config_manager = util::ConfigManager::Instance();
  const std::string ca_bundle_path = config_manager->LocalCertPath();

  std::unique_lock<std::mutex> wait_lock(ca_monitor_mutex_);
  while (!ca_monitor_stop_.load()) {
    if (ca_monitor_cv_.wait_for(
            wait_lock, kCARefreshInterval,
            [this]() { return ca_monitor_stop_.load(); })) {
      break;
    }

    wait_lock.unlock();
    const auto result = PlatformCABundle::Refresh(ca_bundle_path);
    if (result == PlatformCABundle::UpdateResult::kUpdated) {
      LOG(INFO) << "Platform roots changed; reloading gRPC TLS credentials";
      if (!ReloadTLSChannel()) {
        LOG(ERROR) << "Failed to reload gRPC TLS credentials";
      }
    }
    wait_lock.lock();
  }
}

void GrpcClient::StopPlatformCAMonitor() {
  ca_monitor_stop_.store(true);
  ca_monitor_cv_.notify_all();
  if (ca_monitor_thread_.joinable() &&
      ca_monitor_thread_.get_id() != std::this_thread::get_id()) {
    ca_monitor_thread_.join();
    LOG(INFO) << "Platform CA bundle monitor stopped";
  }
}

bool GrpcClient::ReloadTLSChannel() {
  std::lock_guard<std::mutex> lock(lifecycle_mutex_);
  if (ca_monitor_stop_.load()) {
    return false;
  }

  StopManagers();
  AuthenticationManager::Instance()->ClearToken();
  if (!Init()) {
    return false;
  }
  StartManagers();
  return true;
}

}  // namespace client
}  // namespace tbox
