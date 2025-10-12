/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/impl/config_manager.h"

namespace tbox {
namespace util {

static folly::Singleton<ConfigManager> config_manager;

std::shared_ptr<ConfigManager> ConfigManager::Instance() {
  return config_manager.try_get();
}

bool ConfigManager::Init(const std::string& base_config_path) {
  std::string content;
  Util::LoadSmallFile(base_config_path, &content);
  if (!Util::JsonToMessage(content, &base_config_)) {
    LOG(ERROR) << "Parse base config error, path: " << base_config_path
               << ", content: " << content;
    return false;
  }
  LOG(INFO) << "Base config: " << ToString();
  return Validate();
}

bool ConfigManager::Validate() const {
  // Validate server address
  if (base_config_.server_addr().empty()) {
    LOG(ERROR) << "Server address not configured";
    return false;
  }

  // Validate gRPC port
  uint32_t grpc_port = base_config_.grpc_server_port();
  if (grpc_port == 0 || grpc_port > 65535) {
    LOG(ERROR) << "Invalid gRPC port: " << grpc_port
               << " (must be between 1 and 65535)";
    return false;
  }

  // Validate check interval
  uint32_t check_interval = base_config_.check_interval_seconds();
  if (check_interval == 0) {
    LOG(WARNING) << "Check interval not configured or invalid, "
                 << "will use default value (30 seconds)";
  }

  return true;
}

}  // namespace util
}  // namespace tbox
