/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_HTTP_HANDLER_HTTP_HANDLER_FACTORY_H_
#define TBOX_SERVER_HTTP_HANDLER_HTTP_HANDLER_FACTORY_H_

#include "proxygen/httpserver/RequestHandler.h"
#include "proxygen/httpserver/RequestHandlerFactory.h"

namespace tbox {
namespace server {
namespace http_handler {

class HTTPHandlerFactory : public proxygen::RequestHandlerFactory {
 public:
  void onServerStart(folly::EventBase*) noexcept override {}

  void onServerStop() noexcept override { LOG(INFO) << "HTTP server stopped"; }

  proxygen::RequestHandler* onRequest(
      proxygen::RequestHandler*, proxygen::HTTPMessage* msg) noexcept override {
    const std::string& path = msg->getPath();
    const std::string& method = msg->getMethodString();

    // No handlers registered for now
    return nullptr;
  }
};

}  // namespace http_handler
}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_HTTP_HANDLER_HTTP_HANDLER_FACTORY_H_
