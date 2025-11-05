/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_GRPC_HANDLER_CERT_HANDLER_H_
#define TBOX_SERVER_GRPC_HANDLER_CERT_HANDLER_H_

#include <memory>

#include "glog/logging.h"
#include "src/async_grpc/rpc_handler.h"
#include "src/server/grpc_handler/meta.h"
#include "src/server/handler/handler.h"

namespace tbox {
namespace server {
namespace grpc_handler {

class CertOpHandler : public async_grpc::RpcHandler<CertOpMethod> {
 public:
  CertOpHandler() = default;
  ~CertOpHandler() = default;

  void OnRequest(const proto::CertRequest& req) override {
    auto res = std::make_unique<proto::CertResponse>();

    // Use shared handler for certificate operations
    handler::Handler::CertOpHandle(req, res.get());

    Send(std::move(res));
  }

  void OnReadsDone() override { Finish(grpc::Status::OK); }
};

}  // namespace grpc_handler
}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_GRPC_HANDLER_CERT_HANDLER_H_
