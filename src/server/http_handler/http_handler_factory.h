/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_HTTP_HANDLER_HTTP_HANDLER_FACTORY_H_
#define TBOX_SERVER_HTTP_HANDLER_HTTP_HANDLER_FACTORY_H_

#include "proxygen/httpserver/RequestHandler.h"
#include "proxygen/httpserver/RequestHandlerFactory.h"
#include "glog/logging.h"
#include "src/server/http_handler/default_handler.h"
// #include "src/server/http_handler/server_handler.h"
#include "src/server/http_handler/user_handler.h"

namespace tbox {
namespace server {
namespace http_handler {

class HTTPHandlerFactory : public proxygen::RequestHandlerFactory {
 public:
  void onServerStart(folly::EventBase*) noexcept override {}

  void onServerStop() noexcept override { LOG(INFO) << "HTTP server stopped"; }

  proxygen::RequestHandler* onRequest(
      proxygen::RequestHandler*, proxygen::HTTPMessage* msg) noexcept override {
        if (msg->getPath() == "/user") {
          return new UserHandler();
        // } else if (msg->getPath() == "/server") {
        //   return new ServerHandler();
        } else {
          return new DefaultHandler();
        }
        return nullptr;
  }
};

}  // namespace http_handler
}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_HTTP_HANDLER_HTTP_HANDLER_FACTORY_H_
