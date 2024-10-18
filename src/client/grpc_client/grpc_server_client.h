/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_CLIENT_GRPC_STATUS_CLIENT_H
#define TBOX_CLIENT_GRPC_STATUS_CLIENT_H

#include <memory>

#include "glog/logging.h"
#include "grpcpp/client_context.h"
#include "grpcpp/grpcpp.h"
#include "grpcpp/support/client_callback.h"
#include "src/proto/service.grpc.pb.h"
#include "src/proto/service.pb.h"

namespace tbox {
namespace client {

class ServerClient {
 public:
  explicit ServerClient(const std::string& addr, const std::string& port)
      : channel_(grpc::CreateChannel(addr + ":" + port,
                                     grpc::InsecureChannelCredentials())),
        stub_(tbox::proto::OceanFile::NewStub(channel_)) {}

  bool Status(const proto::ServerReq& req, proto::ServerRes* res) {
    grpc::ClientContext context;
    auto status = stub_->ServerOp(&context, req, res);
    if (!status.ok()) {
      LOG(ERROR) << "Grpc error";
      return false;
    }
    return true;
  }

 private:
  std::shared_ptr<grpc::Channel> channel_;
  std::unique_ptr<tbox::proto::OceanFile::Stub> stub_;
};

}  // namespace client
}  // namespace tbox

#endif  // TBOX_CLIENT_GRPC_STATUS_CLIENT_H
