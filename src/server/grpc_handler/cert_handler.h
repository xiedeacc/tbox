/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_GRPC_HANDLER_CERT_HANDLER_H_
#define TBOX_SERVER_GRPC_HANDLER_CERT_HANDLER_H_

#include <memory>

#include "src/common/logging.h"
#include "src/async_grpc/rpc_handler.h"
#include "src/proto/service.pb.h"
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

    try {
      switch (req.op()) {
        case proto::OpCode::OP_CERT_GET:
        case proto::OpCode::OP_GET_PRIVATE_KEY_HASH:
        case proto::OpCode::OP_GET_PRIVATE_KEY:
        case proto::OpCode::OP_GET_FULLCHAIN_CERT_HASH:
        case proto::OpCode::OP_GET_CA_CERT_HASH:
        case proto::OpCode::OP_GET_FULLCHAIN_CERT:
        case proto::OpCode::OP_GET_CA_CERT:
          res->set_err_code(proto::ErrCode::Unsupported_op);
          res->set_message(
              "Legacy certificate operations are disabled; use the "
              "allowlisted certificate file API");
          break;
        case proto::OpCode::OP_GET_CERT_FILE_HASH:
          handler::Handler::HandleGetCertFileHash(req, res.get());
          break;
        case proto::OpCode::OP_GET_CERT_FILE:
          handler::Handler::HandleGetCertFile(req, res.get());
          break;
        default:
          res->set_err_code(proto::ErrCode::Fail);
          res->set_message("Invalid operation code for certificate management");
          LOG(ERROR) << "Invalid certificate operation code: " << req.op();
          break;
      }
    } catch (const std::exception& e) {
      LOG(ERROR) << "Certificate operation failed: " << e.what();
      res->set_err_code(proto::ErrCode::Fail);
      res->set_message(std::string("Operation failed: ") + e.what());
    }

    Send(std::move(res));
    // For NORMAL_RPC, Send() already finishes the RPC, so no need to call
    // Finish() here
  }

  // For NORMAL_RPC, OnReadsDone() is called immediately after OnRequest(),
  // but Send() already handles finishing the RPC, so this can be empty
  void OnReadsDone() override {}
};

}  // namespace grpc_handler
}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_GRPC_HANDLER_CERT_HANDLER_H_
