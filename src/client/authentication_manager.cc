/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/client/authentication_manager.h"

#include <chrono>

#include "openssl/rand.h"
#include "src/common/ssh_key_auth.h"
#include "src/impl/config_manager.h"
#include "src/util/util.h"

namespace tbox {
namespace client {

std::shared_ptr<AuthenticationManager> AuthenticationManager::Instance() {
  static std::shared_ptr<AuthenticationManager> instance(
      new AuthenticationManager());
  return instance;
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

  auto config = tbox::util::ConfigManager::Instance();
  std::string client_nonce(32, '\0');
  if (RAND_bytes(reinterpret_cast<unsigned char*>(client_nonce.data()),
                 static_cast<int>(client_nonce.size())) != 1) {
    LOG(ERROR) << "Failed to generate login nonce";
    return false;
  }

  tbox::proto::UserRequest challenge_req;
  challenge_req.set_request_id(config->ClientId());
  challenge_req.set_op(tbox::proto::OpCode::OP_USER_LOGIN_CHALLENGE);
  challenge_req.set_user(config->User());
  challenge_req.set_client_nonce(client_nonce);

  tbox::proto::UserResponse challenge_res;
  grpc::ClientContext challenge_context;
  auto status =
      stub_->UserOp(&challenge_context, challenge_req, &challenge_res);

  if (!status.ok()) {
    LOG(ERROR) << "Login challenge gRPC error: " << status.error_code()
               << " - "
               << status.error_message();
    return false;
  }

  if (challenge_res.err_code() != tbox::proto::ErrCode::Success ||
      challenge_res.challenge_id().empty() ||
      challenge_res.server_nonce().size() != 32 ||
      challenge_res.server_signature().empty()) {
    LOG(ERROR) << "Login challenge server error: "
               << challenge_res.err_code();
    return false;
  }

  const int64_t now =
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  if (challenge_res.challenge_expires_at() <= now) {
    LOG(ERROR) << "Login challenge is already expired";
    return false;
  }

  const std::string message = common::SshKeyAuth::BuildLoginMessage(
      challenge_res.challenge_id(), client_nonce,
      challenge_res.server_nonce(), config->User(), config->ClientId());
  LOG(INFO) << "Received login challenge transcript "
            << util::Util::SHA256(message).substr(0, 16)
            << ", signature "
            << util::Util::SHA256(challenge_res.server_signature())
                   .substr(0, 16);
  if (!common::SshKeyAuth::VerifyServerProof(
          config->SshPrivateKeyPath(), config->SshPublicKeyPath(), message,
          challenge_res.server_signature())) {
    LOG(ERROR) << "Server SSH key login proof is invalid";
    return false;
  }

  std::string client_signature;
  if (!common::SshKeyAuth::CreateClientProof(
          config->SshPrivateKeyPath(), config->SshPublicKeyPath(), message,
          &client_signature)) {
    LOG(ERROR) << "Failed to create login proof with the client SSH key";
    return false;
  }

  tbox::proto::UserRequest req;
  req.set_request_id(config->ClientId());
  req.set_op(tbox::proto::OpCode::OP_USER_LOGIN);
  req.set_user(config->User());
  req.set_password(config->Password());
  req.set_challenge_id(challenge_res.challenge_id());
  req.set_signature(client_signature);

  tbox::proto::UserResponse res;
  grpc::ClientContext context;
  status = stub_->UserOp(&context, req, &res);
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
