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

/**
 * @brief Configuration manager for application settings.
 *
 * Singleton class that manages application configuration loaded from JSON
 * files. Thread-safe singleton implementation using folly::Singleton.
 */
class ConfigManager {
 private:
  friend class folly::Singleton<ConfigManager>;
  ConfigManager() = default;

 public:
  /**
   * @brief Get singleton instance.
   * @return Shared pointer to ConfigManager instance.
   */
  static std::shared_ptr<ConfigManager> Instance();

  /**
   * @brief Initialize configuration from file.
   * @param base_config_path Path to JSON configuration file.
   * @return true if initialization successful, false otherwise.
   */
  bool Init(const std::string& base_config_path);

  /**
   * @brief Validate configuration values.
   * @return true if configuration is valid, false otherwise.
   */
  bool Validate() const;

  /**
   * @brief Get server address.
   * @return Server address string.
   */
  std::string ServerAddr() const { return base_config_.server_addr(); }

  /**
   * @brief Get HTTP server port.
   * @return HTTP server port number.
   */
  uint32_t HttpServerPort() const { return base_config_.http_server_port(); }

  /**
   * @brief Get gRPC server port.
   * @return gRPC server port number.
   */
  uint32_t GrpcServerPort() const { return base_config_.grpc_server_port(); }

  /**
   * @brief Get gRPC thread count.
   * @return Number of gRPC threads (default: 3).
   */
  uint32_t GrpcThreads() const {
    uint32_t threads = base_config_.grpc_threads();
    return threads > 0 ? threads : 3;  // Default to 3 if not set
  }

  /**
   * @brief Get event thread count.
   * @return Number of event threads (default: 5).
   */
  uint32_t EventThreads() const {
    uint32_t threads = base_config_.event_threads();
    return threads > 0 ? threads : 5;  // Default to 5 if not set
  }

  /**
   * @brief Get client worker thread pool size.
   * @return Thread pool size.
   */
  uint32_t ClientWorkerThreadPoolSize() const {
    return base_config_.client_worker_thread_pool_size();
  }

  /**
   * @brief Get check interval in seconds (for both IP reporting and DDNS).
   * @return Check interval (default: 30 seconds).
   */
  uint32_t CheckIntervalSeconds() const {
    uint32_t interval = base_config_.check_interval_seconds();
    return interval > 0 ? interval : 30;  // Default to 30 seconds
  }

  /**
   * @brief Get authentication username.
   * @return Username string.
   */
  std::string User() const { return base_config_.user(); }

  /**
   * @brief Get authentication password.
   * @return Password string.
   */
  std::string Password() const { return base_config_.password(); }

  /**
   * @brief Get client ID for identification.
   * @return Client ID string.
   */
  std::string ClientId() const { return base_config_.client_id(); }

  /**
   * @brief Get Route53 hosted zone ID.
   * @return Hosted zone ID string.
   */
  std::string Route53HostedZoneId() const {
    return base_config_.route53_hosted_zone_id();
  }

  /**
   * @brief Get AWS access key ID.
   * @return AWS access key ID string.
   */
  std::string AwsAccessKeyId() const {
    return base_config_.aws_access_key_id();
  }

  /**
   * @brief Get AWS secret access key.
   * @return AWS secret access key string.
   */
  std::string AwsSecretAccessKey() const {
    return base_config_.aws_secret_access_key();
  }

  /**
   * @brief Get AWS region.
   * @return AWS region string.
   */
  std::string AwsRegion() const { return base_config_.aws_region(); }

  /**
   * @brief Get monitor domains list.
   * @return Vector of monitor domain strings.
   */
  std::vector<std::string> MonitorDomains() const {
    std::vector<std::string> domains;
    for (const auto& domain : base_config_.monitor_domains()) {
      domains.push_back(domain);
    }
    return domains;
  }

  /**
   * @brief Convert configuration to JSON string.
   * @return JSON representation of configuration.
   */
  std::string ToString() const {
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
