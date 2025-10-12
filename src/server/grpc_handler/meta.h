/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef tbox_SERVER_GRPC_HANDLER_META_H_
#define tbox_SERVER_GRPC_HANDLER_META_H_

#include "src/proto/service.pb.h"

namespace tbox {
namespace server {
namespace grpc_handler {

struct EC2OpMethod {
  static constexpr const char* MethodName() {
    return "/tbox.proto.TBOXService/EC2Op";
  }
  using IncomingType = tbox::proto::EC2Request;
  using OutgoingType = tbox::proto::EC2Response;
};

struct ReportOpMethod {
  static constexpr const char* MethodName() {
    return "/tbox.proto.TBOXService/ReportOp";
  }
  using IncomingType = tbox::proto::ReportRequest;
  using OutgoingType = tbox::proto::ReportResponse;
};

struct UserOpMethod {
  static constexpr const char* MethodName() {
    return "/tbox.proto.TBOXService/UserOp";
  }
  using IncomingType = tbox::proto::UserRequest;
  using OutgoingType = tbox::proto::UserResponse;
};

}  // namespace grpc_handler
}  // namespace server
}  // namespace tbox

#endif  // tbox_SERVER_GRPC_HANDLER_META_H_
