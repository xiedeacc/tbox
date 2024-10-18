/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_CONTEXT_SERVER_CONTEXT_H
#define TBOX_CONTEXT_SERVER_CONTEXT_H

#include "fmt/core.h"
#include "glog/logging.h"
#include "src/async_grpc/execution_context.h"
#include "src/server/version_info.h"
#include "src/util/config_manager.h"
#include "src/util/util.h"

// TODO city, operator, code
namespace tbox {
namespace server {

using EchoResponder = std::function<bool()>;

class ServerContext : public async_grpc::ExecutionContext {
 public:
  ServerContext()
      : git_commit_(GIT_VERSION), uptime_(util::Util::CurrentTimeMillis()) {}

  void MarkedGrpcServerInitedDone() {
    LOG(INFO) << "Grpc server started on: "
              << util::ConfigManager::Instance()->ServerAddr() << ", port: "
              << util::ConfigManager::Instance()->GrpcServerPort();
  }

  void MarkedHttpServerInitedDone() {
    LOG(INFO) << (util::ConfigManager::Instance()->UseHttps() ? "Https"
                                                              : "Http")
              << " server started on: "
              << util::ConfigManager::Instance()->ServerAddr() << ", port: "
              << util::ConfigManager::Instance()->HttpServerPort();
  }

  std::string ToString() {
    std::string info;
    info.reserve(1024);
    info.append(fmt::format("uptime: {}\n", util::Util::ToTimeStr(uptime_)));
    info.append(fmt::format("git commit: {}\n", git_commit_));
    info.append(
        fmt::format("server current time: {}\n", util::Util::ToTimeStr()));
    return info;
  }

 private:
  std::string git_commit_;
  const int64_t uptime_;
};

}  // namespace server
}  // namespace tbox

#endif  // TBOX_CONTEXT_SERVER_CONTEXT_H
