/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_CLIENT_GRPC_CLIENT_H_
#define TBOX_CLIENT_GRPC_CLIENT_H_

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "glog/logging.h"
#include "grpcpp/grpcpp.h"
#include "src/async_grpc/client.h"
#include "src/proto/service.grpc.pb.h"
#include "src/server/grpc_handler/meta.h"
#include "src/util/config_manager.h"
#include "src/util/util.h"

namespace tbox {
namespace client {

class GrpcClient {
 public:
  GrpcClient(const std::string& host, int port,
             int report_interval_seconds = 30)
      : host_(host),
        port_(port),
        target_address_(host + ":" + std::to_string(port)),
        report_interval_seconds_(report_interval_seconds),
        running_(false),
        should_stop_(false) {

    grpc::SslCredentialsOptions ssl_opts;
    ssl_opts.pem_root_certs = LoadCACert("/conf/xiedeacc.com.ca.cer");
    // ssl_opts.pem_cert_chain = LoadCACert("/conf/xiedeacc.com.fullchain.cer");

    auto channel_creds = grpc::SslCredentials(ssl_opts);

    grpc::ChannelArguments args;
    args.SetSslTargetNameOverride("xiedeacc.com");
    args.SetString(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG, "xiedeacc.com");

    // Use SSL channel for production
    channel_ = grpc::CreateCustomChannel(target_address_, channel_creds, args);
    stub_ = tbox::proto::TBOXService::NewStub(channel_);

    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(10);
    if (channel_->WaitForConnected(deadline)) {
      LOG(INFO) << "Connected to " << host << ":" << port << " successfully";
    } else {
      LOG(WARNING) << "Failed to connect to " << host << ":" << port
                   << " within timeout";
    }
  }

  ~GrpcClient() { Stop(); }

  // Stop the background thread
  void Stop() {
    if (!running_) {
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

  // Start the background thread for periodic IP reporting
  void Start() {
    if (running_) {
      LOG(WARNING) << "IP reporting thread is already running";
      return;
    }

    should_stop_ = false;
    running_ = true;

    reporting_thread_ = std::thread(&GrpcClient::ReportingLoop, this);
    LOG(INFO) << "Started IP reporting thread with interval "
              << report_interval_seconds_ << " seconds";
  }

 private:
  std::string LoadCACert(const std::string& path) {
    std::string home_dir = tbox::util::Util::HomeDir();
    std::ifstream file(home_dir + path);
    std::string ca_cert((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    LOG(INFO) << ca_cert;
    return ca_cert;
  }

  bool Login() {
    tbox::proto::UserRequest req;
    tbox::proto::UserResponse res;
    req.set_request_id(tbox::util::Util::UUID());
    req.set_op(tbox::proto::OpCode::OP_USER_LOGIN);
    req.set_user(tbox::util::ConfigManager::Instance()->User());
    req.set_password(tbox::util::ConfigManager::Instance()->Password());

    grpc::ClientContext context;
    auto status = stub_->UserOp(&context, req, &res);
    if (!status.ok()) {
      LOG(ERROR) << "Grpc error: " << status.error_code() << " - "
                 << status.error_message();
      return false;
    }
    if (res.err_code() != tbox::proto::ErrCode::Success) {
      LOG(ERROR) << "Server error: " << res.err_code();
      return false;
    }
    token_ = res.token();
    LOG(INFO) << "Login success, token: " << token_;
    return true;
  }

  // Get all local IP addresses of this client
  std::vector<std::string> GetLocalIPAddresses() {
    struct ifaddrs* ifaddrs_ptr;
    std::vector<std::string> ip_addresses;

    if (getifaddrs(&ifaddrs_ptr) == -1) {
      LOG(ERROR) << "Failed to get network interfaces";
      return ip_addresses;
    }

    for (struct ifaddrs* ifa = ifaddrs_ptr; ifa != nullptr;
         ifa = ifa->ifa_next) {
      if (ifa->ifa_addr == nullptr) continue;

      // Check for IPv4 address
      if (ifa->ifa_addr->sa_family == AF_INET) {
        struct sockaddr_in* sa = (struct sockaddr_in*)ifa->ifa_addr;
        char* addr = inet_ntoa(sa->sin_addr);

        // Skip loopback address but include all other interfaces
        if (strcmp(addr, "127.0.0.1") != 0) {
          std::string ip_str(addr);
          // Avoid duplicates
          if (std::find(ip_addresses.begin(), ip_addresses.end(), ip_str) ==
              ip_addresses.end()) {
            ip_addresses.push_back(ip_str);
            LOG(INFO) << "Detected IP address: " << ip_str
                      << " on interface: " << ifa->ifa_name;
          }
        }
      }
    }

    freeifaddrs(ifaddrs_ptr);

    if (ip_addresses.empty()) {
      LOG(WARNING) << "No non-loopback IP addresses found, adding 'unknown'";
      ip_addresses.push_back("unknown");
    }

    return ip_addresses;
  }

  // Report all client IP addresses to the server
  bool ReportClientIP() {
    std::vector<std::string> client_ips = GetLocalIPAddresses();
    try {
      async_grpc::Client<tbox::server::grpc_handler::ReportOpMethod> client(
          channel_);

      tbox::proto::ReportRequest request;
      request.set_op(tbox::proto::OpCode::OP_REPORT);
      request.set_token(token_);

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
      }
    } catch (const std::exception& e) {
      LOG(ERROR) << "Exception while reporting client IP: " << e.what();
    }
    return false;
  }

  // The main loop that runs in the background thread
  void ReportingLoop() {
    LOG(INFO) << "IP reporting loop started";

    // Step 2: Login to server
    if (!Login()) {
      LOG(WARNING) << "Failed to login, will retry in 60 seconds";
      return;
    }

    while (true) {
      // Check if we should stop
      {
        std::unique_lock<std::mutex> lock(mutex_);
        if (cv_.wait_for(lock, std::chrono::seconds(report_interval_seconds_),
                         [this] { return should_stop_.load(); })) {
          // should_stop_ became true, exit loop
          break;
        }
      }

      // Report client IP addresses
      try {
        bool success = ReportClientIP();
        if (!success) {
          LOG(WARNING) << "Failed to report client IP in background thread, "
                          "will retry in "
                       << report_interval_seconds_ << " seconds";
        } else {
          LOG(INFO) << "Successfully reported client IP in background thread";
        }
      } catch (const std::exception& e) {
        LOG(ERROR) << "Exception in IP reporting loop: " << e.what();
      }
    }

    LOG(INFO) << "IP reporting loop ended";
  }

 private:
  // Connection info
  std::string host_;
  int port_;
  std::string target_address_;
  std::shared_ptr<grpc::Channel> channel_;
  std::unique_ptr<tbox::proto::TBOXService::Stub> stub_;

  // Authentication
  std::string token_;

  // Thread management for IP reporting
  int report_interval_seconds_;
  std::atomic<bool> running_;
  std::atomic<bool> should_stop_;
  std::thread reporting_thread_;
  std::mutex mutex_;
  std::condition_variable cv_;
};

}  // namespace client
}  // namespace tbox

#endif  // TBOX_CLIENT_GRPC_CLIENT_H_
