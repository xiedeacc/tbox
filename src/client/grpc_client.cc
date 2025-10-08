/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/client/grpc_client.h"

#include <chrono>
#include <exception>
#include <vector>

#include "glog/logging.h"
#include "src/async_grpc/client.h"
#include "src/client/authentication_manager.h"
#include "src/client/network_info_provider.h"
#include "src/client/ssl_config_loader.h"
#include "src/proto/service.grpc.pb.h"
#include "src/server/grpc_handler/meta.h"

namespace tbox {
namespace client {

GrpcClient::GrpcClient(const GrpcClientConfig& config)
    : config_(config),
      target_address_(config.host + ":" + std::to_string(config.port)),
      running_(false),
      should_stop_(false) {
  if (!InitializeChannel()) {
    LOG(ERROR) << "Failed to initialize gRPC channel";
  }
}

GrpcClient::~GrpcClient() { Stop(); }

bool GrpcClient::InitializeChannel() {
  std::shared_ptr<grpc::ChannelCredentials> channel_creds;
  grpc::ChannelArguments args;

  // Use insecure channel for localhost/127.0.0.1, SSL for remote
  if (config_.host == "localhost" || config_.host == "127.0.0.1" || 
      config_.host == "::1") {
    LOG(INFO) << "Using insecure channel for local connection";
    channel_creds = grpc::InsecureChannelCredentials();
  } else {
    // Load SSL certificate for remote connections
    std::string ca_cert = SSLConfigLoader::LoadCACert(config_.ca_cert_path);
    if (ca_cert.empty()) {
      LOG(ERROR) << "Failed to load CA certificate from: " << config_.ca_cert_path;
      return false;
    }

    // Configure SSL
    grpc::SslCredentialsOptions ssl_opts;
    ssl_opts.pem_root_certs = ca_cert;
    channel_creds = grpc::SslCredentials(ssl_opts);

    // Configure channel arguments for SSL
    args.SetSslTargetNameOverride(config_.ssl_target_name);
    args.SetString(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG, config_.ssl_target_name);
    LOG(INFO) << "Using SSL channel for remote connection";
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
  auto deadline = std::chrono::system_clock::now() +
                  std::chrono::seconds(config_.connection_timeout_seconds);
  if (channel_->WaitForConnected(deadline)) {
    LOG(INFO) << "Connected to " << config_.host << ":" << config_.port
              << " successfully";
  } else {
    LOG(WARNING) << "Failed to connect to " << config_.host << ":"
                 << config_.port << " within "
                 << config_.connection_timeout_seconds << " seconds timeout";
    // Don't return false - we'll retry in the reporting loop
  }

  // Initialize authentication manager
  auth_manager_ = std::make_unique<AuthenticationManager>(stub_);

  return true;
}

void GrpcClient::Start() {
  if (running_.load()) {
    LOG(WARNING) << "IP reporting thread is already running";
    return;
  }

  should_stop_ = false;
  running_ = true;

  reporting_thread_ = std::thread(&GrpcClient::ReportingLoop, this);
  LOG(INFO) << "Started IP reporting thread with interval "
            << config_.report_interval_seconds << " seconds";
}

void GrpcClient::Stop() {
  if (!running_.load()) {
    return;
  }

  LOG(INFO) << "Stopping IP reporting thread...";

  // Signal the thread to stop
  {
    std::lock_guard<std::mutex> lock(mutex_);
    should_stop_ = true;
  }
  cv_.notify_all();

  // Wait for the thread to finish
  if (reporting_thread_.joinable()) {
    reporting_thread_.join();
  }

  running_ = false;
  LOG(INFO) << "IP reporting thread stopped";
}

bool GrpcClient::ReportClientIP() {
  if (!auth_manager_) {
    LOG(ERROR) << "Authentication manager not initialized";
    return false;
  }

  if (!auth_manager_->IsAuthenticated()) {
    LOG(WARNING) << "Not authenticated, cannot report client IP";
    return false;
  }

  std::vector<std::string> client_ips =
      NetworkInfoProvider::GetLocalIPv6Addresses();

  try {
    async_grpc::Client<tbox::server::grpc_handler::ReportOpMethod> client(
        channel_);

    tbox::proto::ReportRequest request;
    request.set_op(tbox::proto::OpCode::OP_REPORT);
    request.set_token(auth_manager_->GetToken());

    // Add all client IP addresses
    for (const auto& ip : client_ips) {
      request.add_client_ip(ip);
    }

    request.set_timestamp(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    request.set_client_info("TBox C++ Client");

    grpc::Status status;
    bool success = client.Write(request, &status);

    if (success && status.ok()) {
      return true;
    } else {
      LOG(WARNING) << "Report failed - status: " << status.error_code()
                   << ", message: " << status.error_message();
    }
  } catch (const std::exception& e) {
    LOG(ERROR) << "Exception while reporting client IP: " << e.what();
  }
  
  return false;
}

void GrpcClient::ReportingLoop() {
  LOG(INFO) << "IP reporting loop started";

  // Attempt login with retry logic
  while (!should_stop_.load()) {
    if (auth_manager_ && auth_manager_->Login()) {
      LOG(INFO) << "Successfully logged in";
      break;
    }

    LOG(WARNING) << "Failed to login, will retry in "
                 << config_.login_retry_seconds << " seconds";

    // Wait before retry, but check for stop signal
    std::unique_lock<std::mutex> lock(mutex_);
    if (cv_.wait_for(lock, std::chrono::seconds(config_.login_retry_seconds),
                     [this] { return should_stop_.load(); })) {
      // should_stop_ became true, exit loop
      LOG(INFO) << "Login retry interrupted by stop signal";
      return;
    }
  }

  // Main reporting loop
  while (true) {
    // Wait for the configured interval or until stop signal
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (cv_.wait_for(lock,
                       std::chrono::seconds(config_.report_interval_seconds),
                       [this] { return should_stop_.load(); })) {
        // should_stop_ became true, exit loop
        break;
      }
    }

    // Report client IP addresses
    try {
      bool success = ReportClientIP();
      if (!success) {
        LOG(WARNING) << "Failed to report client IP, will retry in "
                     << config_.report_interval_seconds << " seconds";
      } else {
        LOG(INFO) << "Successfully reported client IP";
      }
    } catch (const std::exception& e) {
      LOG(ERROR) << "Exception in IP reporting loop: " << e.what();
    }
  }

  LOG(INFO) << "IP reporting loop ended";
}

}  // namespace client
}  // namespace tbox

