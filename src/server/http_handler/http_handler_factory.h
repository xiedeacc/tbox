/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_HTTP_HANDLER_HTTP_HANDLER_FACTORY_H_
#define TBOX_SERVER_HTTP_HANDLER_HTTP_HANDLER_FACTORY_H_

#include "glog/logging.h"
#include "proxygen/httpserver/RequestHandler.h"
#include "proxygen/httpserver/RequestHandlerFactory.h"
#include "src/server/http_handler/default_handler.h"
#include "src/server/http_handler/server_handler.h"
#include "src/server/http_handler/user_handler.h"

namespace tbox {
namespace server {
namespace http_handler {

/**
 * @brief Factory for creating HTTP request handlers based on URI path.
 *
 * Maps exact paths to concrete handlers:
 * - "/user"   -> `UserHandler`
 * - "/server" -> `ServerHandler`
 * All other paths fall back to `DefaultHandler` which returns 404.
 */
class HTTPHandlerFactory : public proxygen::RequestHandlerFactory {
 public:
  void onServerStart(folly::EventBase*) noexcept override {}

  void onServerStop() noexcept override { LOG(INFO) << "HTTP server stopped"; }

  /**
   * @brief Select a handler for the incoming request based on path.
   */
  proxygen::RequestHandler* onRequest(
      proxygen::RequestHandler*, proxygen::HTTPMessage* msg) noexcept override {
    if (msg->getPath() == "/user") {
      return new UserHandler();
    } else if (msg->getPath() == "/server") {
      return new ServerHandler();
    } else {
      return new DefaultHandler();
    }
  }
};

}  // namespace http_handler
}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_HTTP_HANDLER_HTTP_HANDLER_FACTORY_H_
