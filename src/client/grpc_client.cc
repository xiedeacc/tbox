/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/client/grpc_client.h"

#include <chrono>
#include <exception>
#include <vector>

#include "glog/logging.h"
#include "src/client/authentication_manager.h"
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
    auto ssl_config_manager = SSLConfigManager::Instance();

    // Update SSL config manager with current configuration
    ssl_config_manager->UpdateConfig(config_manager->GetBaseConfig());
    
    // Set gRPC channel for SSL config manager to use for server communication
    ssl_config_manager->SetChannel(channel_);

    // Get certificate path from configuration
    std::string ca_cert_path = config_manager->LocalCertPath();
    if (ca_cert_path.empty()) {
      ca_cert_path = "conf/xiedeacc.com.ca.cer";  // Default fallback
    }

    std::string ca_cert = ssl_config_manager->LoadCACert(ca_cert_path);
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
  // Start SSL config manager (if certificate updates are enabled)
  auto ssl_config_manager = SSLConfigManager::Instance();
  ssl_config_manager->Start();
  LOG(INFO) << "SSL config manager started";

  // Start report manager
  auto report_manager = ReportManager::Instance();
  if (!report_manager->IsRunning()) {
    report_manager->Start();
    LOG(INFO) << "Report manager started";
  }
}

void GrpcClient::Stop() {
  LOG(INFO) << "Stopping GrpcClient...";

  // Stop SSL config manager
  auto ssl_config_manager = SSLConfigManager::Instance();
  ssl_config_manager->Stop();
  LOG(INFO) << "SSL config manager stopped";

  // Stop report manager
  auto report_manager = ReportManager::Instance();
  if (report_manager->IsRunning()) {
    report_manager->Stop();
    LOG(INFO) << "Report manager stopped";
  }

  LOG(INFO) << "GrpcClient stopped";
}

bool GrpcClient::IsRunning() const {
  auto report_manager = ReportManager::Instance();
  return report_manager->IsRunning();
}

}  // namespace client
}  // namespace tbox
