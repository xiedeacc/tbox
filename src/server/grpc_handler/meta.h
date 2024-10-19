/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_GRPC_HANDLERS_META_H
#define TBOX_SERVER_GRPC_HANDLERS_META_H

#include "src/proto/service.pb.h"

namespace tbox {
namespace server {
namespace grpc_handler {

struct UserMethod {
  static constexpr const char* MethodName() {
    return "/tbox.proto.TboxService/UserOp";
  }
  using IncomingType = tbox::proto::UserReq;
  using OutgoingType = tbox::proto::UserRes;
};

struct ServerMethod {
  static constexpr const char* MethodName() {
    return "/tbox.proto.TboxService/ServerOp";
  }
  using IncomingType = tbox::proto::ServerReq;
  using OutgoingType = tbox::proto::ServerRes;
};

}  // namespace grpc_handler
}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_GRPC_HANDLERS_META_H
