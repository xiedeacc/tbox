/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_GRPC_HANDLERS_META_H
#define TBOX_SERVER_GRPC_HANDLERS_META_H

#include "src/async_grpc/type_traits.h"
#include "src/proto/service.pb.h"

namespace tbox {
namespace server {
namespace grpc_handler {

struct ServerMethod {
  static constexpr const char* MethodName() {
    return "/tbox.proto.OceanFile/ServerOp";
  }
  using IncomingType = tbox::proto::ServerReq;
  using OutgoingType = tbox::proto::ServerRes;
};

}  // namespace grpc_handler
}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_GRPC_HANDLERS_META_H
