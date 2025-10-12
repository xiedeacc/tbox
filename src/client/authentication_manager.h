/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_CLIENT_AUTHENTICATION_MANAGER_H_
#define TBOX_CLIENT_AUTHENTICATION_MANAGER_H_

#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include "folly/Singleton.h"
#include "glog/logging.h"
#include "grpcpp/grpcpp.h"
#include "src/impl/config_manager.h"
#include "src/proto/service.grpc.pb.h"
#include "src/util/util.h"

namespace tbox {
namespace client {

/// @brief Manages user authentication and token lifecycle.
/// @details Thread-safe singleton that handles login, token storage,
///          expiration tracking, and automatic refresh. Tokens
///          expire after a configurable duration (default: 24 hours)
///          and auto-refresh when nearing expiration (< 10%).
class AuthenticationManager final {
 public:
  /// @brief Get singleton instance.
  /// @return Shared pointer to AuthenticationManager instance.
  static std::shared_ptr<AuthenticationManager> Instance();

  ~AuthenticationManager() = default;

  /// @brief Initialize with gRPC stub.
  /// @param stub Shared pointer to TBOXService gRPC stub.
  /// @note Thread-safe initialization.
  void Init(std::shared_ptr<tbox::proto::TBOXService::Stub> stub);

  /// @brief Perform login and store authentication token.
  /// @return True if login is successful, false otherwise.
  /// @note Retrieves credentials from ConfigManager.
  bool Login();

  /// @brief Get current authentication token (thread-safe).
  /// @return Authentication token string, or empty if not authenticated.
  /// @note Automatically refreshes token if close to expiration (< 10%).
  std::string GetToken();

  /// @brief Check if authenticated with a valid, non-expired token.
  /// @return True if a valid token exists, false otherwise.
  bool IsAuthenticated() const;

  /// @brief Clear authentication token.
  /// @note Thread-safe token clearing.
  void ClearToken();

  /// @brief Set token expiration duration (default: 24 hours).
  /// @param duration_seconds Time in seconds before token expires.
  void SetTokenExpirationDuration(int64_t duration_seconds);

  /// @brief Check if token needs refresh (within 10% of expiration).
  /// @return True if refresh is recommended, false otherwise.
  bool NeedsRefresh() const;

 private:
  friend class folly::Singleton<AuthenticationManager>;
  AuthenticationManager() : stub_(nullptr) {}

  std::shared_ptr<tbox::proto::TBOXService::Stub> stub_;
  mutable std::mutex init_mutex_;
  mutable std::mutex token_mutex_;
  std::string token_;

  // Token expiration tracking
  int64_t token_expiration_time_millis_ = 0;       // Unix timestamp in millis
  int64_t token_duration_seconds_ = 24 * 60 * 60;  // Default: 24 hours
};

}  // namespace client
}  // namespace tbox

#endif  // TBOX_CLIENT_AUTHENTICATION_MANAGER_H_
