/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_HTTP_HANDLER_SERVER_HANDLER_H
#define TBOX_SERVER_HTTP_HANDLER_SERVER_HANDLER_H

#include "proxygen/httpserver/RequestHandler.h"
#include "src/server/handler/handler.h"
#include "src/server/http_handler/util.h"

namespace tbox {
namespace server {
namespace http_handler {

class ServerHandler : public proxygen::RequestHandler {
 public:
  void onRequest(
      std::unique_ptr<proxygen::HTTPMessage> headers) noexcept override {}
  void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override {
    body_.append(reinterpret_cast<const char*>(body->data()), body->length());
  }
  void onEOM() noexcept override {
    // proto::ServerReq req;
    // proto::ServerRes res;
    // std::string res_body = "Parse request error";

    // if (!util::Util::JsonToMessage(body_, &req)) {
    //   LOG(INFO) << body_;
    //   Util::InternalServerError(res_body, downstream_);
    //   return;
    // }

    // handler_proxy::HandlerProxy::ServerOpHandle(req, &res);

    // res_body.clear();
    // if (!util::Util::MessageToJson(res, &res_body)) {
    //   res_body = "Res pb to json error";
    //   Util::InternalServerError(res_body, downstream_);
    //   return;
    // }
    // Util::Success(res_body, downstream_);
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

#endif  // TBOX_SERVER_HTTP_HANDLER_SERVER_HANDLER_H
