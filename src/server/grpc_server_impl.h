/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_GRPC_SERVER_IMPL_H_
#define TBOX_SERVER_GRPC_SERVER_IMPL_H_

#include <memory>
#include <string>

#include "absl/strings/str_cat.h"
#include "src/async_grpc/server.h"
#include "src/impl/config_manager.h"
#include "src/server/grpc_handler/cert_handler.h"
#include "src/server/grpc_handler/report_handler.h"
#include "src/server/grpc_handler/server_handler.h"
#include "src/server/grpc_handler/user_handler.h"
#include "src/server/server_context.h"

namespace tbox {
namespace server {

class GrpcServer final {
 public:
  GrpcServer(std::shared_ptr<ServerContext> server_context)
      : terminated(false) {
    async_grpc::Server::Builder server_builder;
    std::string addr_port =
        util::ConfigManager::Instance()->ServerAddr() + ":" +
        absl::StrCat(util::ConfigManager::Instance()->GrpcServerPort());
    server_builder.SetServerAddress(addr_port);
    server_builder.SetNumGrpcThreads(
        util::ConfigManager::Instance()->GrpcThreads());
    server_builder.SetNumEventThreads(
        util::ConfigManager::Instance()->EventThreads());

    // Register handlers
    server_builder
        .RegisterHandler<tbox::server::grpc_handler::ReportOpHandler>();
    server_builder.RegisterHandler<tbox::server::grpc_handler::UserHandler>();
    server_builder.RegisterHandler<tbox::server::grpc_handler::CertOpHandler>();
    server_builder.RegisterHandler<tbox::server::grpc_handler::ServerOpHandler>();

    server_ = server_builder.Build();
    server_->SetExecutionContext(server_context);
  }

 public:
  void Start() {
    server_->Start();
    server_->GetContext<ServerContext>()->MarkedGrpcServerInitedDone();
  }

  void Shutdown() { server_->Shutdown(); }
  void WaitForShutdown() { server_->WaitForShutdown(); }

 private:
  std::unique_ptr<async_grpc::Server> server_;

 public:
  std::atomic_bool terminated;
};

}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_GRPC_SERVER_IMPL_H_
