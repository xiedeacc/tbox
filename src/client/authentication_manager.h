/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_CLIENT_AUTHENTICATION_MANAGER_H_
#define TBOX_CLIENT_AUTHENTICATION_MANAGER_H_

#include <memory>
#include <mutex>
#include <string>

#include "glog/logging.h"
#include "grpcpp/grpcpp.h"
#include "src/proto/service.grpc.pb.h"
#include "src/util/config_manager.h"
#include "src/util/util.h"

namespace tbox {
namespace client {

class AuthenticationManager {
 public:
  explicit AuthenticationManager(
      std::unique_ptr<tbox::proto::TBOXService::Stub>& stub)
      : stub_(stub) {}

  // Perform login and store authentication token
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
      LOG(ERROR) << "Login gRPC error: " << status.error_code() << " - "
                 << status.error_message();
      return false;
    }
    
    if (res.err_code() != tbox::proto::ErrCode::Success) {
      LOG(ERROR) << "Login server error: " << res.err_code();
      return false;
    }

    // Thread-safe token update
    {
      std::lock_guard<std::mutex> lock(token_mutex_);
      token_ = res.token();
    }
    
    LOG(INFO) << "Login successful, token received";
    return true;
  }

  // Get current authentication token (thread-safe)
  std::string GetToken() const {
    std::lock_guard<std::mutex> lock(token_mutex_);
    return token_;
  }

  // Check if authenticated
  bool IsAuthenticated() const {
    std::lock_guard<std::mutex> lock(token_mutex_);
    return !token_.empty();
  }

  // Clear authentication token
  void ClearToken() {
    std::lock_guard<std::mutex> lock(token_mutex_);
    token_.clear();
  }

 private:
  std::unique_ptr<tbox::proto::TBOXService::Stub>& stub_;
  mutable std::mutex token_mutex_;
  std::string token_;
};

}  // namespace client
}  // namespace tbox

#endif  // TBOX_CLIENT_AUTHENTICATION_MANAGER_H_

