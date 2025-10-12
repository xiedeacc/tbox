/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_HANDLER_PROXY_H
#define TBOX_SERVER_HANDLER_PROXY_H

#include <utility>

#include "aws/core/Aws.h"
#include "aws/route53/Route53Client.h"
#include "aws/route53/model/Change.h"
#include "aws/route53/model/ChangeBatch.h"
#include "aws/route53/model/ChangeResourceRecordSetsRequest.h"
#include "aws/route53/model/ResourceRecord.h"
#include "aws/route53/model/ResourceRecordSet.h"
#include "src/impl/session_manager.h"
#include "src/impl/user_manager.h"
#include "src/proto/service.pb.h"
#include "src/util/util.h"

namespace tbox {
namespace server {
namespace handler {

class Handler {
 public:
  static absl::base_internal::SpinLock kLock;

  // static int32_t UpdateDNSRecord(const proto::ServerReq& req) {
  //   Aws::SDKOptions options;
  //   Aws::InitAPI(options);

  //   {
  //     Aws::Client::ClientConfiguration client_config;
  //     client_config.connectTimeoutMs = 2000;  // 2 seconds to connect
  //     client_config.requestTimeoutMs = 3000;
  //     client_config.httpRequestTimeoutMs = 2000;

  //     Aws::Route53::Route53Client client(client_config);

  //     Aws::Route53::Model::ChangeResourceRecordSetsRequest request;
  //     request.SetHostedZoneId(req.hosted_zone_id());

  //     Aws::Route53::Model::Change change;
  //     change.SetAction(Aws::Route53::Model::ChangeAction::UPSERT);

  //     Aws::Route53::Model::ResourceRecordSet record_set;
  //     record_set.SetName(req.record_name());
  //     record_set.SetType(Aws::Route53::Model::RRType(req.record_type()));
  //     record_set.SetTTL(60);

  //     Aws::Route53::Model::ResourceRecord record;
  //     record.SetValue(req.record_value());
  //     record_set.AddResourceRecords(record);

  //     change.SetResourceRecordSet(record_set);

  //     Aws::Route53::Model::ChangeBatch change_batch;
  //     change_batch.AddChanges(change);

  //     request.SetChangeBatch(change_batch);

  //     auto outcome = client.ChangeResourceRecordSets(request);
  //     if (outcome.IsSuccess()) {
  //       LOG(INFO) << "Record updated successfully";
  //     } else {
  //       LOG(ERROR) << "Error updating record: "
  //                  << outcome.GetError().GetMessage();
  //       return Err_Dns_update_recored_error;
  //     }
  //   }
  //   Aws::ShutdownAPI(options);
  //   return Err_Success;
  // }

  // static void ServerOpHandle(const proto::ServerReq& req,
  //                            proto::ServerRes* res) {
  //   std::string session_user;
  //   if (req.token().empty()) {
  //     res->set_err_code(proto::ErrCode(Err_User_session_error));
  //     return;
  //   }
  //   if (!impl::SessionManager::Instance()->ValidateSession(req.token(),
  //                                                          &session_user)) {
  //     res->set_err_code(proto::ErrCode(Err_User_session_error));
  //     return;
  //   }
  //   if (!session_user.empty() && session_user != req.user()) {
  //     res->set_err_code(proto::ErrCode(Err_User_session_error));
  //     return;
  //   }

  //   int32_t ret = Err_Success;
  //   switch (req.op()) {
  //     case proto::ServerOp::ServerUpdateDevIp:
  //       ret = UpdateDevAddrs(req);
  //       break;
  //     case proto::ServerOp::ServerUpdateDevDns:
  //       if (!req.context().outer_ipv4().empty() ||
  //           !req.context().outer_ipv6().empty()) {
  //         UpdateDevAddrs(req);
  //       }
  //       ret = UpdateDNSRecord(req);
  //       break;
  //     case proto::ServerOp::ServerGetDevIp:
  //       util::Util::MessageToJson(kDevIPAddrs, res->mutable_err_msg(), true);
  //       break;
  //     default:
  //       ret = Err_Unsupported_op;
  //       LOG(ERROR) << "Unsupported operation";
  //   }

  //   if (ret) {
  //     res->set_err_code(proto::ErrCode(ret));
  //   } else {
  //     res->set_err_code(proto::ErrCode::Success);
  //   }
  // }

  static void UserOpHandle(const proto::UserRequest& req,
                           proto::UserResponse* res) {
    // Log request_id (client_id) for login operations
    if (req.op() == proto::OpCode::OP_USER_LOGIN ||
        req.op() == proto::OpCode::OP_USER_CREATE) {
      LOG(INFO) << "User operation: " << req.op()
                << ", Client ID: " << req.request_id()
                << ", User: " << req.user();
    }

    std::string session_user;
    if (req.token().empty() && req.op() != proto::OpCode::OP_USER_CREATE &&
        req.op() != proto::OpCode::OP_USER_LOGIN) {
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
      case proto::OpCode::OP_USER_CREATE:
        ret = impl::UserManager::Instance()->UserRegister(
            req.user(), req.password(), res->mutable_token());
        break;
      case proto::OpCode::OP_USER_DELETE:
        ret = impl::UserManager::Instance()->UserDelete(
            session_user, req.to_delete_user(), req.token());
        break;
      case proto::OpCode::OP_USER_LOGIN:
        ret = impl::UserManager::Instance()->UserLogin(
            req.user(), req.password(), res->mutable_token());
        break;
      case proto::OpCode::OP_USER_CHANGE_PASSWORD:
        ret = impl::UserManager::Instance()->ChangePassword(
            req.user(), req.password(), res->mutable_token());
        break;
      case proto::OpCode::OP_USER_LOGOUT:
        ret = impl::UserManager::Instance()->UserLogout(req.token());
        break;
      default:
        ret = Err_Unsupported_op;
        LOG(ERROR) << "Unsupported operation";
    }

    if (ret) {
      res->set_err_code(proto::ErrCode(ret));
    } else {
      res->set_err_code(proto::ErrCode::Success);
    }
  }
};

}  // namespace handler
}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_HANDLER_PROXY_H
