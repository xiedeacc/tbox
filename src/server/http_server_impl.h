/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_HTTP_SERVER_IMPL_H
#define TBOX_SERVER_HTTP_SERVER_IMPL_H

#include <memory>
#include <string>

#include "folly/io/async/SSLContext.h"
#include "proxygen/httpserver/HTTPServer.h"
#include "proxygen/httpserver/HTTPServerOptions.h"
#include "src/server/http_handler/http_handler_factory.h"
#include "src/server/server_context.h"
#include "src/util/config_manager.h"

namespace tbox {
namespace server {

class HttpServer final {
 public:
  HttpServer(std::shared_ptr<ServerContext> server_context)
      : server_context_(server_context) {
    proxygen::HTTPServerOptions options;
    options.threads = static_cast<size_t>(sysconf(_SC_NPROCESSORS_ONLN));
    options.idleTimeout = std::chrono::milliseconds(60000);
    options.handlerFactories =
        proxygen::RequestHandlerChain()
            .addThen<tbox::server::http_handler::HTTPHandlerFactory>()
            .build();

    std::string addr = util::ConfigManager::Instance()->ServerAddr();
    int32_t port = util::ConfigManager::Instance()->HttpServerPort();

    proxygen::HTTPServer::IPConfig ip_config{
        folly::SocketAddress(addr, port, true),
        proxygen::HTTPServer::Protocol::HTTP};

    std::vector<proxygen::HTTPServer::IPConfig> IPs{ip_config};

    server_ = std::make_shared<proxygen::HTTPServer>(std::move(options));
    server_->bind(IPs);
  }

 public:
  void Start() {
    server_context_->MarkedHttpServerInitedDone();
    server_->start();
  }
  void Shutdown() { server_->stop(); }

 private:
  std::shared_ptr<proxygen::HTTPServer> server_;
  std::shared_ptr<ServerContext> server_context_;
};

}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_HTTP_SERVER_IMPL_H
