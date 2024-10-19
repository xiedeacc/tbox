/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_UTIL_CONFIG_MANAGER_H
#define TBOX_UTIL_CONFIG_MANAGER_H

#include <memory>
#include <string>

#include "folly/Singleton.h"
#include "glog/logging.h"
#include "src/proto/config.pb.h"
#include "src/util/util.h"

namespace tbox {
namespace util {

class ConfigManager {
 private:
  friend class folly::Singleton<ConfigManager>;
  ConfigManager() = default;

 public:
  static std::shared_ptr<ConfigManager> Instance();

  bool Init(const std::string& base_config_path) {
    std::string content;
    auto ret = Util::LoadSmallFile(base_config_path, &content);
    if (ret && !Util::JsonToMessage(content, &base_config_)) {
      LOG(ERROR) << "parse base config error, path: " << base_config_path
                 << ", content: " << content;
      return false;
    }
    LOG(INFO) << "base config: " << ToString();
    return true;
  }

  std::string ServerAddr() { return base_config_.server_addr(); }
  std::string ServerDomain() { return base_config_.server_domain(); }
  uint32_t GrpcServerPort() { return base_config_.grpc_server_port(); }
  uint32_t HttpServerPort() { return base_config_.http_server_port(); }
  uint32_t GrpcThreads() { return base_config_.grpc_threads(); }
  uint32_t EventThreads() { return base_config_.event_threads(); }
  bool UseHttps() { return base_config_.use_https(); }
  std::string User() { return base_config_.user(); }
  std::string Password() { return base_config_.password(); }

  std::string ToString() {
    std::string json;
    Util::MessageToJson(base_config_, &json);
    return json;
  }

 private:
  tbox::proto::BaseConfig base_config_;
};

}  // namespace util
}  // namespace tbox

#endif  // TBOX_UTIL_CONFIG_MANAGER_H
