/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_GRPC_HANDLER_CERT_HANDLER_H_
#define TBOX_SERVER_GRPC_HANDLER_CERT_HANDLER_H_

#include <memory>

#include "glog/logging.h"
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
          handler::Handler::HandleGetCertificate(req, res.get());
          break;
        case proto::OpCode::OP_GET_PRIVATE_KEY_HASH:
          handler::Handler::HandleGetPrivateKeyHash(req, res.get());
          break;
        case proto::OpCode::OP_GET_PRIVATE_KEY:
          handler::Handler::HandleGetPrivateKey(req, res.get());
          break;
        case proto::OpCode::OP_GET_FULLCHAIN_CERT_HASH:
          handler::Handler::HandleGetFullchainCertHash(req, res.get());
          break;
        case proto::OpCode::OP_GET_CA_CERT_HASH:
          handler::Handler::HandleGetCACertHash(req, res.get());
          break;
        case proto::OpCode::OP_GET_FULLCHAIN_CERT:
          handler::Handler::HandleGetFullchainCert(req, res.get());
          break;
        case proto::OpCode::OP_GET_CA_CERT:
          handler::Handler::HandleGetCACert(req, res.get());
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
  }

  void OnReadsDone() override { Finish(grpc::Status::OK); }
};

}  // namespace grpc_handler
}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_GRPC_HANDLER_CERT_HANDLER_H_
