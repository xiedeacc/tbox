/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_HTTP_HANDLER_CERT_HANDLER_H
#define TBOX_SERVER_HTTP_HANDLER_CERT_HANDLER_H

#include "glog/logging.h"
#include "proxygen/httpserver/RequestHandler.h"
#include "src/proto/service.pb.h"
#include "src/server/handler/handler.h"
#include "src/server/http_handler/util.h"

namespace tbox {
namespace server {
namespace http_handler {

class CertHandler : public proxygen::RequestHandler {
 public:
  void onRequest(
      std::unique_ptr<proxygen::HTTPMessage> headers) noexcept override {}

  void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override {
    body_.append(reinterpret_cast<const char*>(body->data()), body->length());
  }

  void onEOM() noexcept override {
    proto::CertRequest req;
    proto::CertResponse res;
    std::string res_body = "Parse request error";

    if (!util::Util::JsonToMessage(body_, &req)) {
      LOG(INFO) << body_;
      Util::InternalServerError(res_body, downstream_);
      return;
    }

    // Handle certificate request using shared handler
    try {
      switch (req.op()) {
        case proto::OpCode::OP_CERT_GET:
          handler::Handler::HandleGetCertificate(req, &res);
          break;
        case proto::OpCode::OP_GET_PRIVATE_KEY_HASH:
          handler::Handler::HandleGetPrivateKeyHash(req, &res);
          break;
        case proto::OpCode::OP_GET_PRIVATE_KEY:
          handler::Handler::HandleGetPrivateKey(req, &res);
          break;
        case proto::OpCode::OP_GET_FULLCHAIN_CERT_HASH:
          handler::Handler::HandleGetFullchainCertHash(req, &res);
          break;
        case proto::OpCode::OP_GET_CA_CERT_HASH:
          handler::Handler::HandleGetCACertHash(req, &res);
          break;
        case proto::OpCode::OP_GET_FULLCHAIN_CERT:
          handler::Handler::HandleGetFullchainCert(req, &res);
          break;
        case proto::OpCode::OP_GET_CA_CERT:
          handler::Handler::HandleGetCACert(req, &res);
          break;
        default:
          res.set_err_code(proto::ErrCode::Fail);
          res.set_message("Invalid operation code for certificate management");
          LOG(ERROR) << "Invalid certificate operation code: " << req.op();
          break;
      }
    } catch (const std::exception& e) {
      LOG(ERROR) << "Certificate operation failed: " << e.what();
      res.set_err_code(proto::ErrCode::Fail);
      res.set_message(std::string("Operation failed: ") + e.what());
    }

    res_body.clear();
    if (!util::Util::MessageToJson(res, &res_body)) {
      res_body = "Response pb to json error";
      Util::InternalServerError(res_body, downstream_);
      return;
    }
    Util::Success(res_body, downstream_);
  }

  void onUpgrade(proxygen::UpgradeProtocol protocol) noexcept override {}
  void requestComplete() noexcept override {}
  void onError(proxygen::ProxygenError err) noexcept override {}

 private:
  std::string body_;
};


}  // namespace http_handler
}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_HTTP_HANDLER_CERT_HANDLER_H
