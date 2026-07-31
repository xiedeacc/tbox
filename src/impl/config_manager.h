/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_UTIL_CONFIG_MANAGER_H_
#define TBOX_UTIL_CONFIG_MANAGER_H_

#include <memory>
#include <string>

#include "src/common/logging.h"
#include "src/proto/config.pb.h"
#include "src/util/util.h"

namespace tbox {
namespace util {

/**
 * @brief Configuration manager for application settings.
 *
 * Singleton class that manages application configuration loaded from JSON
 * files. Thread-safe singleton implementation.
 */
class ConfigManager {
 private:
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
   * @brief Get the configured DNS backend name.
   * @return Backend name, or "route53" when unset.
   */
  std::string DnsProvider() const {
    const std::string& provider = base_config_.dns_provider();
    return provider.empty() ? "route53" : provider;
  }

  /**
   * @brief Get Cloudflare API token.
   * @return Cloudflare API token string.
   */
  std::string CloudflareApiToken() const {
    return base_config_.cloudflare_api_token();
  }

  /**
   * @brief Get Cloudflare zone ID.
   * @return Cloudflare zone ID string, empty when it must be resolved.
   */
  std::string CloudflareZoneId() const {
    return base_config_.cloudflare_zone_id();
  }

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
   * @brief Get configured DDNS record types.
   * @return Record type names; empty means both A and AAAA.
   */
  std::vector<std::string> DdnsRecordTypes() const {
    std::vector<std::string> types;
    for (const auto& type : base_config_.ddns_record_types()) {
      types.push_back(type);
    }
    return types;
  }

  /**
   * @brief Get embedded vlmcsd enable flag.
   * @return true if vlmcsd should be started.
   */
  bool VlmcsdEnabled() const { return base_config_.vlmcsd_enabled(); }

  /**
   * @brief Get embedded vlmcsd listen addresses.
   * @return Configured addresses, or 127.0.0.1 when enabled and unset.
   */
  std::vector<std::string> VlmcsdListenAddresses() const {
    std::vector<std::string> addresses;
    for (const auto& address : base_config_.vlmcsd_listen_addresses()) {
      addresses.push_back(address);
    }
    if (addresses.empty() && base_config_.vlmcsd_enabled()) {
      addresses.push_back("127.0.0.1");
    }
    return addresses;
  }

  /**
   * @brief Get certificate update flag.
   * @return true if certificate updates are enabled, false otherwise.
   */
  bool UpdateCerts() const { return base_config_.update_certs(); }

  /**
   * @brief Get local certificate path.
   * @return Local certificate path string.
   */
  std::string LocalCertPath() const { return base_config_.local_cert_path(); }

  /**
   * @brief Get nginx SSL path.
   * @return Nginx SSL directory path string.
   */
  std::string NginxSslPath() const { return base_config_.nginx_ssl_path(); }

  /**
   * @brief Get certificate filenames synchronized from the server.
   */
  std::vector<std::string> CertificateFiles() const {
    std::vector<std::string> files;
    for (const auto& file : base_config_.certificate_files()) {
      files.push_back(file);
    }
    return files;
  }

  std::vector<std::string> CertificateSyncClientIds() const {
    std::vector<std::string> ids;
    for (const auto& id : base_config_.certificate_sync_client_ids()) {
      ids.push_back(id);
    }
    return ids;
  }

  std::string CertificatePath() const {
    if (!base_config_.certificate_path().empty()) {
      return base_config_.certificate_path();
    }
    if (!base_config_.nginx_ssl_path().empty()) {
      return base_config_.nginx_ssl_path();
    }
#if defined(_WIN32)
    return "D:\\software\\tbox\\conf\\ssl";
#else
    return "/etc/nginx/ssl";
#endif
  }

  /**
   * @brief Get the Ed25519 SSH private key path.
   */
  std::string SshPrivateKeyPath() const {
    const auto& path = base_config_.ssh_private_key_path();
    return path.empty() ? "~/.ssh/id_ed25519" : path;
  }

  /**
   * @brief Get the Ed25519 SSH public key path.
   */
  std::string SshPublicKeyPath() const {
    const auto& path = base_config_.ssh_public_key_path();
    return path.empty() ? "~/.ssh/id_ed25519.pub" : path;
  }

  /**
   * @brief Get base configuration object.
   * @return Reference to base configuration.
   */
  const tbox::proto::BaseConfig& GetBaseConfig() const { return base_config_; }

  /**
   * @brief Convert configuration to JSON string.
   * @return JSON representation of configuration.
   */
  std::string ToString() const {
    std::string json;
    Util::MessageToJson(base_config_, &json);
    return json;
  }

  /**
   * @brief Convert configuration to JSON with credentials redacted.
   * @return JSON representation safe for application logs.
   */
  std::string ToRedactedString() const {
    tbox::proto::BaseConfig redacted_config = base_config_;
    constexpr char kRedacted[] = "[REDACTED]";
    if (!redacted_config.password().empty()) {
      redacted_config.set_password(kRedacted);
    }
    if (!redacted_config.aws_access_key_id().empty()) {
      redacted_config.set_aws_access_key_id(kRedacted);
    }
    if (!redacted_config.aws_secret_access_key().empty()) {
      redacted_config.set_aws_secret_access_key(kRedacted);
    }
    if (!redacted_config.cloudflare_api_token().empty()) {
      redacted_config.set_cloudflare_api_token(kRedacted);
    }

    std::string json;
    Util::MessageToJson(redacted_config, &json);
    return json;
  }

 private:
  tbox::proto::BaseConfig base_config_;
};

}  // namespace util
}  // namespace tbox

#endif  // TBOX_UTIL_CONFIG_MANAGER_H_
