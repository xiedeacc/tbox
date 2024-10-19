/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_HANDLER_PROXY_H
#define TBOX_SERVER_HANDLER_PROXY_H

#include <utility>

#include "src/impl/session_manager.h"
#include "src/impl/user_manager.h"
#include "src/proto/service.pb.h"
#include "src/util/util.h"

namespace tbox {
namespace server {
namespace handler_proxy {

class HandlerProxy {
 public:
  static proto::Context kDevIPAddrs;
  static absl::base_internal::SpinLock kLock;

  static void UpdateDevAddrs(const proto::ServerReq& req) {
    absl::base_internal::SpinLockHolder locker(&kLock);
    kDevIPAddrs = std::move(req.context());
    LOG(INFO) << "Receive public ipv6 addrs num: "
              << kDevIPAddrs.public_ipv6().size();
  }

  static void ServerOpHandle(const proto::ServerReq& req,
                             proto::ServerRes* res) {
    std::string session_user;
    if (req.token().empty()) {
      res->set_err_code(proto::ErrCode(Err_User_session_error));
      return;
    }
    if (!impl::SessionManager::Instance()->ValidateSession(req.token(),
                                                           &session_user)) {
      res->set_err_code(proto::ErrCode(Err_User_session_error));
      return;
    }
    if (!session_user.empty() && session_user != req.user()) {
      res->set_err_code(proto::ErrCode(Err_User_session_error));
      return;
    }

    int32_t ret = Err_Success;
    switch (req.op()) {
      case proto::ServerOp::ServerUpdateDevIp:
        UpdateDevAddrs(req);
        break;
      case proto::ServerOp::ServerUpdateDevDns:
        break;
      case proto::ServerOp::ServerGetDevIp:
        util::Util::MessageToJson(kDevIPAddrs, res->mutable_err_msg(), true);
        break;
      default:
        LOG(ERROR) << "Unsupported operation";
    }

    if (ret) {
      res->set_err_code(proto::ErrCode(ret));
    } else {
      res->set_err_code(proto::ErrCode::Success);
    }
  }

  static void UserOpHandle(const proto::UserReq& req, proto::UserRes* res) {
    std::string session_user;
    if (req.token().empty() && req.op() != proto::UserOp::UserCreate &&
        req.op() != proto::UserOp::UserLogin) {
      res->set_err_code(proto::ErrCode(Err_User_session_error));
    }
    if (!req.token().empty() &&
        !impl::SessionManager::Instance()->ValidateSession(req.token(),
                                                           &session_user)) {
      res->set_err_code(proto::ErrCode(Err_User_session_error));
      return;
    }
    if (!session_user.empty() && session_user != req.user()) {
      res->set_err_code(proto::ErrCode(Err_User_session_error));
      return;
    }

    int32_t ret = Err_Success;

    switch (req.op()) {
      case proto::UserOp::UserCreate:
        ret = impl::UserManager::Instance()->UserRegister(
            req.user(), req.password(), res->mutable_token());
        break;
      case proto::UserOp::UserDel:
        ret = impl::UserManager::Instance()->UserDelete(
            session_user, req.to_delete_user(), req.token());
        break;
      case proto::UserOp::UserLogin:
        ret = impl::UserManager::Instance()->UserLogin(
            req.user(), req.password(), res->mutable_token());
        break;
      case proto::UserOp::UserChangePassword:
        ret = impl::UserManager::Instance()->ChangePassword(
            req.user(), req.password(), res->mutable_token());
        break;
      case proto::UserOp::UserLogout:
        ret = impl::UserManager::Instance()->UserLogout(req.token());
        break;
      default:
        LOG(ERROR) << "Unsupported operation";
    }

    if (ret) {
      res->set_err_code(proto::ErrCode(ret));
    } else {
      res->set_err_code(proto::ErrCode::Success);
    }
  }
};

}  // namespace handler_proxy
}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_HANDLER_PROXY_H
