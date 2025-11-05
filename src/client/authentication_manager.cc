/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/client/authentication_manager.h"

#include "src/impl/config_manager.h"
#include "src/util/util.h"

namespace tbox {
namespace client {

static folly::Singleton<AuthenticationManager> authentication_manager;

std::shared_ptr<AuthenticationManager> AuthenticationManager::Instance() {
  return authentication_manager.try_get();
}

void AuthenticationManager::Init(
    std::shared_ptr<tbox::proto::TBOXService::Stub> stub) {
  std::lock_guard<std::mutex> lock(init_mutex_);
  stub_ = stub;
}

bool AuthenticationManager::Login() {
  if (!stub_) {
    LOG(ERROR) << "AuthenticationManager not initialized with stub";
    return false;
  }

  tbox::proto::UserRequest req;
  tbox::proto::UserResponse res;

  auto config = tbox::util::ConfigManager::Instance();
  req.set_request_id(config->ClientId());
  req.set_op(tbox::proto::OpCode::OP_USER_LOGIN);
  req.set_user(config->User());
  // Hash password with SHA-256 before sending to server
  req.set_password(util::Util::SHA256(config->Password()));

  grpc::ClientContext context;
  auto status = stub_->UserOp(&context, req, &res);

  if (!status.ok()) {
    LOG(ERROR) << "Login gRPC error: " << status.error_code() << " - "
               << status.error_message();
    return false;
  }

  if (res.err_code() != tbox::proto::ErrCode::Success) {
    LOG(ERROR) << "Login server error: " << res.err_code();
    return false;
  }

  // Thread-safe token update with expiration time
  {
    std::lock_guard<std::mutex> lock(token_mutex_);
    token_ = res.token();
    token_expiration_time_millis_ =
        util::Util::CurrentTimeMillis() + (token_duration_seconds_ * 1000);
  }

  LOG(INFO) << "Login successful, token received (expires in "
            << token_duration_seconds_ << " seconds)";
  return true;
}

std::string AuthenticationManager::GetToken() {
  std::unique_lock<std::mutex> lock(token_mutex_);

  // Check if token needs refresh
  if (!token_.empty() && NeedsRefresh()) {
    LOG(INFO) << "Token approaching expiration, refreshing...";
    lock.unlock();  // Unlock before calling Login to avoid deadlock

    // Attempt to refresh token
    if (Login()) {
      LOG(INFO) << "Token refreshed successfully";
    } else {
      LOG(WARNING) << "Token refresh failed, using existing token";
    }

    lock.lock();  // Re-acquire lock to return token
  }

  return token_;
}

bool AuthenticationManager::IsAuthenticated() const {
  std::lock_guard<std::mutex> lock(token_mutex_);

  if (token_.empty()) {
    return false;
  }

  // Check if token has expired
  int64_t now_millis = util::Util::CurrentTimeMillis();
  if (now_millis >= token_expiration_time_millis_) {
    LOG(WARNING) << "Token has expired";
    return false;
  }

  return true;
}

void AuthenticationManager::ClearToken() {
  std::lock_guard<std::mutex> lock(token_mutex_);
  token_.clear();
  token_expiration_time_millis_ = 0;
}

void AuthenticationManager::SetTokenExpirationDuration(
    int64_t duration_seconds) {
  std::lock_guard<std::mutex> lock(token_mutex_);
  token_duration_seconds_ = duration_seconds;
  LOG(INFO) << "Token expiration duration set to " << duration_seconds
            << " seconds";
}

bool AuthenticationManager::NeedsRefresh() const {
  if (token_.empty()) {
    return false;
  }

  int64_t now_millis = util::Util::CurrentTimeMillis();
  int64_t time_until_expiration_seconds =
      (token_expiration_time_millis_ - now_millis) / 1000;

  // Refresh if within 10% of expiration time
  int64_t refresh_threshold = token_duration_seconds_ / 10;
  return time_until_expiration_seconds < refresh_threshold;
}

}  // namespace client
}  // namespace tbox
