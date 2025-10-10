/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/client/report_manager.h"

#include <chrono>
#include <exception>
#include <vector>

#include "glog/logging.h"
#include "src/async_grpc/client.h"
#include "src/client/authentication_manager.h"
#include "src/server/grpc_handler/meta.h"
#include "src/util/util.h"

namespace tbox {
namespace client {

static folly::Singleton<ReportManager> report_manager;

std::shared_ptr<ReportManager> ReportManager::Instance() {
  return report_manager.try_get();
}

ReportManager::ReportManager()
    : running_(false), should_stop_(false), connection_healthy_(false) {}

bool ReportManager::Init(std::shared_ptr<grpc::Channel> channel,
                         std::shared_ptr<tbox::proto::TBOXService::Stub> stub,
                         int report_interval_seconds, int login_retry_seconds) {
  std::lock_guard<std::mutex> lock(init_mutex_);

  if (initialized_) {
    LOG(WARNING) << "ReportManager already initialized";
    return true;
  }

  channel_ = channel;
  stub_ = stub;
  report_interval_seconds_ = report_interval_seconds;
  login_retry_seconds_ = login_retry_seconds;

  initialized_ = true;
  return true;
}

ReportManager::~ReportManager() {
  Stop();
}

void ReportManager::Start() {
  if (running_.load()) {
    LOG(WARNING) << "Reporting thread is already running";
    return;
  }

  if (!initialized_) {
    LOG(ERROR) << "Cannot start ReportManager without initialization";
    return;
  }

  should_stop_ = false;
  running_ = true;

  reporting_thread_ = std::thread(&ReportManager::ReportingLoop, this);
  LOG(INFO) << "Started IP reporting thread with interval "
            << report_interval_seconds_ << " seconds";
}

void ReportManager::Stop() {
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

bool ReportManager::ReportClientIP() {
  auto auth_manager = client::AuthenticationManager::Instance();
  if (!auth_manager->IsAuthenticated()) {
    LOG(WARNING) << "Not authenticated, cannot report client IP";
    return false;
  }

  std::vector<std::string> client_ips = util::Util::GetAllLocalIPAddresses();

  try {
    async_grpc::Client<tbox::server::grpc_handler::ReportOpMethod> client(
        channel_);

    tbox::proto::ReportRequest request;
    request.set_op(tbox::proto::OpCode::OP_REPORT);
    request.set_token(auth_manager->GetToken());

    // Add all client IP addresses
    for (const auto& ip : client_ips) {
      request.add_client_ip(ip);
    }

    request.set_timestamp(util::Util::CurrentTimeSeconds());
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

void ReportManager::ReportingLoop() {
  LOG(INFO) << "IP reporting loop started";

  auto auth_manager = client::AuthenticationManager::Instance();

  // Attempt login with retry logic
  while (!should_stop_.load()) {
    if (auth_manager->Login()) {
      LOG(INFO) << "Successfully logged in";
      break;
    }

    LOG(WARNING) << "Failed to login, will retry in " << login_retry_seconds_
                 << " seconds";

    // Wait before retry, but check for stop signal
    std::unique_lock<std::mutex> lock(mutex_);
    if (cv_.wait_for(lock, std::chrono::seconds(login_retry_seconds_),
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
      if (cv_.wait_for(lock, std::chrono::seconds(report_interval_seconds_),
                       [this] { return should_stop_.load(); })) {
        // should_stop_ became true, exit loop
        break;
      }
    }

    // Check connection health periodically
    if (!IsConnectionHealthy()) {
      LOG(WARNING) << "Connection unhealthy, attempting reconnect...";
      if (!Reconnect()) {
        LOG(ERROR) << "Reconnection failed, will retry next cycle";
        continue;
      }
    }

    // Report client IP addresses
    try {
      bool success = ReportClientIP();
      if (success) {
        LOG(INFO) << "Successfully reported client IP";
        std::lock_guard<std::mutex> lock(connection_mutex_);
        last_successful_op_millis_ = util::Util::CurrentTimeMillis();
        connection_healthy_.store(true);
      } else {
        LOG(WARNING) << "Failed to report client IP";
        connection_healthy_.store(false);
      }
    } catch (const std::exception& e) {
      LOG(ERROR) << "Exception in reporting loop: " << e.what();
      connection_healthy_.store(false);
    }
  }

  LOG(INFO) << "IP reporting loop ended";
}

bool ReportManager::IsConnectionHealthy() {
  std::lock_guard<std::mutex> lock(connection_mutex_);
  int64_t now_millis = util::Util::CurrentTimeMillis();
  int64_t time_since_last_success_seconds =
      (now_millis - last_successful_op_millis_) / 1000;

  return connection_healthy_.load() &&
         time_since_last_success_seconds < kHealthCheckTimeoutSeconds;
}

bool ReportManager::Reconnect() {
  // For now, just return true as channel reconnection is handled by gRPC
  // We'll mark as healthy and let the next report attempt determine success
  connection_healthy_.store(true);
  return true;
}

}  // namespace client
}  // namespace tbox
