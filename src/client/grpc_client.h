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

/// @brief Manages gRPC connection and client lifecycle.
/// @details Handles connection setup (HTTP/HTTPS), authentication,
///          and integrates with ReportManager for IP reporting and
///          DDNSManager for dynamic DNS updates.
///          Configuration is loaded from ConfigManager singleton.
class GrpcClient {
 public:
  /// @brief Constructor that uses ConfigManager for configuration.
  GrpcClient();
  ~GrpcClient();

  /// @brief Deleted copy constructor.
  GrpcClient(const GrpcClient&) = delete;

  /// @brief Deleted copy assignment operator.
  GrpcClient& operator=(const GrpcClient&) = delete;

  /// @brief Start ReportManager and DDNSManager.
  void Start();

  /// @brief Stop ReportManager and DDNSManager.
  void Stop();

  /// @brief Check if the client is running.
  /// @return True if managers are active, false otherwise.
  bool IsRunning() const;

 private:
  /// @brief Initialize gRPC channel and stub.
  /// @return True on success, false on failure.
  bool Init();

  /// @brief Parse hostname by removing protocol prefix.
  /// @param hostname Hostname possibly with http:// or https:// prefix.
  /// @return Pair of (cleaned_hostname, use_http).
  static std::pair<std::string, bool> ParseHostname(
      const std::string& hostname);

  // Connection components
  std::string target_address_;
  std::shared_ptr<grpc::Channel> channel_;
  std::shared_ptr<tbox::proto::TBOXService::Stub> stub_;
};

}  // namespace client
}  // namespace tbox

#endif  // TBOX_CLIENT_GRPC_CLIENT_H_
