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

struct EC2InstanceManagementMethod {
  static constexpr const char* MethodName() {
    return "/tbox.proto.MEGAService/EC2InstanceManagement";
  }
  using IncomingType = tbox::proto::EC2InstanceRequest;
  using OutgoingType = tbox::proto::EC2InstanceResponse;
};

struct Route53ManagementMethod {
  static constexpr const char* MethodName() {
    return "/tbox.proto.MEGAService/Route53Management";
  }
  using IncomingType = tbox::proto::Route53Request;
  using OutgoingType = tbox::proto::Route53Response;
};

}  // namespace grpc_handler
}  // namespace server
}  // namespace tbox

#endif  // tbox_SERVER_GRPC_HANDLER_META_H_
