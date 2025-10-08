/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_CLIENT_GRPC_CLIENT_H_
#define TBOX_CLIENT_GRPC_CLIENT_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "grpcpp/grpcpp.h"
#include "src/proto/service.grpc.pb.h"

namespace tbox {
namespace client {

// Forward declarations
class AuthenticationManager;

struct GrpcClientConfig {
  std::string host;
  int port;
  int report_interval_seconds = 30;
  int login_retry_seconds = 60;
  int connection_timeout_seconds = 10;
  std::string ca_cert_path = "/conf/xiedeacc.com.ca.cer";
  std::string ssl_target_name = "xiedeacc.com";
};

class GrpcClient {
 public:
  explicit GrpcClient(const GrpcClientConfig& config);
  ~GrpcClient();

  // Delete copy operations
  GrpcClient(const GrpcClient&) = delete;
  GrpcClient& operator=(const GrpcClient&) = delete;

  // Start the background thread for periodic IP reporting
  void Start();

  // Stop the background thread
  void Stop();

  // Check if the client is running
  bool IsRunning() const { return running_.load(); }

 private:
  // Initialize gRPC channel and stub
  bool InitializeChannel();

  // Report client IP addresses to the server
  bool ReportClientIP();

  // The main loop that runs in the background thread
  void ReportingLoop();

  // Configuration
  GrpcClientConfig config_;

  // Connection components
  std::string target_address_;
  std::shared_ptr<grpc::Channel> channel_;
  std::unique_ptr<tbox::proto::TBOXService::Stub> stub_;

  // Authentication manager
  std::unique_ptr<AuthenticationManager> auth_manager_;

  // Thread management for IP reporting
  std::atomic<bool> running_;
  std::atomic<bool> should_stop_;
  std::thread reporting_thread_;
  std::mutex mutex_;
  std::condition_variable cv_;
};

}  // namespace client
}  // namespace tbox

#endif  // TBOX_CLIENT_GRPC_CLIENT_H_

