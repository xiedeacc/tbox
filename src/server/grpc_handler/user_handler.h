/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_GRPC_HANDLERS_USER_HANDLER_H
#define TBOX_SERVER_GRPC_HANDLERS_USER_HANDLER_H

#include "src/async_grpc/rpc_handler.h"
#include "src/proto/service.pb.h"
#include "src/server/grpc_handler/meta.h"
#include "src/server/handler/handler.h"

namespace tbox {
namespace server {
namespace grpc_handler {

class UserHandler : public async_grpc::RpcHandler<UserOpMethod> {
 public:
  void OnRequest(const proto::UserRequest& req) override {
    auto res = std::make_unique<proto::UserResponse>();
    handler::Handler::UserOpHandle(req, res.get());
    Send(std::move(res));
  }

  void OnReadsDone() override { Finish(grpc::Status::OK); }
};

}  // namespace grpc_handler
}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_GRPC_HANDLERS_USER_HANDLER_H
