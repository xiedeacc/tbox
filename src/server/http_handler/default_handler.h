/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_HTTP_HANDLER_DEFAULT_HANDLER_H
#define TBOX_SERVER_HTTP_HANDLER_DEFAULT_HANDLER_H

#include "proxygen/httpserver/RequestHandler.h"
#include "proxygen/httpserver/ResponseBuilder.h"
#include "proxygen/lib/http/HTTPMessage.h"

namespace tbox {
namespace server {
namespace http_handler {

class DefaultHandler : public proxygen::RequestHandler {
 public:
  void onRequest(
      std::unique_ptr<proxygen::HTTPMessage> headers) noexcept override {
    const proxygen::HTTPHeaders& reqHeaders = headers->getHeaders();

    std::cout << "Request Headers:" << std::endl;
    reqHeaders.forEach(
        [&](const std::string& headerName, const std::string& headerValue) {
          std::cout << headerName << ": " << headerValue << std::endl;
        });
  }

  void onBody(std::unique_ptr<folly::IOBuf> /*body*/) noexcept override {
    // Handle the body of the request if needed
  }

  void onEOM() noexcept override {
    // Respond with a 404 Not Found when the request reaches the end
    proxygen::ResponseBuilder(downstream_)
        .status(404, "Not Found")
        .body("The requested path was not found on this server.")
        .sendWithEOM();
  }

  void onUpgrade(proxygen::UpgradeProtocol /*protocol*/) noexcept override {}

  void requestComplete() noexcept override { delete this; }

  void onError(proxygen::ProxygenError /*err*/) noexcept override {
    delete this;
  }
};

}  // namespace http_handler
}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_HTTP_HANDLER_DEFAULT_HANDLER_H
