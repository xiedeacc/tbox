/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_UTIL_CONFIG_MANAGER_H_
#define TBOX_UTIL_CONFIG_MANAGER_H_

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
    Util::LoadSmallFile(base_config_path, &content);
    if (!Util::JsonToMessage(content, &base_config_)) {
      LOG(ERROR) << "Parse base config error, path: " << base_config_path
                 << ", content: " << content;
      return false;
    }
    LOG(INFO) << "Base config: " << ToString();
    return true;
  }

  std::string ServerAddr() { return base_config_.server_addr(); }
  uint32_t HttpServerPort() { return base_config_.http_server_port(); }
  uint32_t GrpcServerPort() { return base_config_.grpc_server_port(); }
  uint32_t ClientWorkerThreadPoolSize() {
    return base_config_.client_worker_thread_pool_size();
  }
  uint32_t ReportIntervalSeconds() { 
    uint32_t interval = base_config_.report_interval_seconds();
    return interval > 0 ? interval : 30; // Default to 30 seconds if not set or zero
  }
  
  // Authentication credentials
  std::string User() { return base_config_.user(); }
  std::string Password() { return base_config_.password(); }
  
  // Route53 settings
  std::string Route53HostedZoneId() { return base_config_.route53_hosted_zone_id(); }
  std::string DomainName() { return base_config_.domain_name(); }

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

#endif  // TBOX_UTIL_CONFIG_MANAGER_H_
