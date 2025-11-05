/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_SERVER_CONTEXT_H_
#define TBOX_SERVER_SERVER_CONTEXT_H_

#include <atomic>
#include <future>

#include "fmt/core.h"
#include "glog/logging.h"
#include "src/async_grpc/execution_context.h"
#include "src/impl/config_manager.h"
#include "src/server/version_info.h"

namespace tbox {
namespace server {

class ServerContext : public async_grpc::ExecutionContext {
 public:
  ServerContext() : is_inited_(false), git_commit_(GIT_VERSION) {}

  void MarkedHttpServerInitedDone() {
    LOG(INFO) << "HTTP server started on: "
              << util::ConfigManager::Instance()->ServerAddr() << ", port: "
              << util::ConfigManager::Instance()->HttpServerPort();
  }

  void MarkedGrpcServerInitedDone() {
    LOG(INFO) << "gRPC server started on: "
              << util::ConfigManager::Instance()->ServerAddr() << ", port: "
              << util::ConfigManager::Instance()->GrpcServerPort();
    is_inited_ = true;
  }

  bool IsInitYet() { return is_inited_.load(); }

  std::string ToString() {
    std::string info;
    info.reserve(1024);
    info.append(fmt::format("git commit: {}\n", git_commit_));
    return info;
  }

 private:
  std::atomic_bool is_inited_;
  std::string git_commit_;
};

}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_SERVER_CONTEXT_H_
