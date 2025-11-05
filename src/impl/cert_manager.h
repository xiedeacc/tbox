/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_IMPL_CERT_MANAGER_H_
#define TBOX_IMPL_CERT_MANAGER_H_

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "folly/Singleton.h"

namespace tbox {
namespace impl {

/// @brief Manages SSL certificate synchronization between acme.sh and nginx.
/// @details Thread-safe singleton that periodically checks certificate files
///          in nginx ssl directory and syncs them from acme.sh directories
///          when missing or outdated (using SHA256 hash comparison).
class CertManager final {
 public:
  /// @brief Get singleton instance.
  /// @return Shared pointer to CertManager instance.
  static std::shared_ptr<CertManager> Instance();

  ~CertManager();

  /// @brief Initialize certificate manager.
  /// @details Sets up certificate paths and validates directories.
  /// @return True on success, false on failure.
  bool Init();

  /// @brief Start the background thread for periodic certificate sync.
  void Start();

  /// @brief Stop the background thread.
  void Stop();

  /// @brief Check if the manager is running.
  /// @return True if the sync thread is active, false otherwise.
  bool IsRunning() const { return running_.load(); }

  /// @brief Perform a single certificate sync check and update if needed.
  /// @return True on success or no update needed, false on failure.
  bool SyncCertificates();

  // Configuration constants
  static constexpr int kCheckIntervalSeconds = 3600;  ///< Check every hour
  static constexpr const char* kNginxSslDir =
      "/etc/nginx/ssl";  ///< Nginx SSL dir
  static constexpr const char* kAcmeBaseDir =
      "/home/ubuntu/.acme.sh";  ///< Acme.sh base dir

  /// @brief Certificate file types
  enum class CertType {
    KEY,       ///< Private key (.key)
    CA,        ///< CA certificate (.ca.cer)
    FULLCHAIN  ///< Fullchain certificate (.fullchain.cer)
  };

  /// @brief Domain configuration
  struct DomainConfig {
    std::string domain;        ///< Domain name (e.g., "xiedeacc.com")
    std::string acme_dir;      ///< Acme.sh directory path
    std::string nginx_prefix;  ///< Nginx file prefix
  };

 private:
  friend class folly::Singleton<CertManager>;
  CertManager();

  /// @brief Get the file extension for a certificate type.
  /// @param type Certificate type.
  /// @return File extension (e.g., ".key", ".ca.cer").
  static std::string GetCertFileExtension(CertType type);

  /// @brief Get the acme.sh filename for a certificate type.
  /// @param domain Domain name.
  /// @param type Certificate type.
  /// @return Acme.sh filename (e.g., "xiedeacc.com.key", "ca.cer").
  static std::string GetAcmeFilename(const std::string& domain, CertType type);

  /// @brief Get the nginx filename for a certificate type.
  /// @param domain Domain name.
  /// @param type Certificate type.
  /// @return Nginx filename (e.g., "xiedeacc.com.key", "xiedeacc.com.ca.cer").
  static std::string GetNginxFilename(const std::string& domain, CertType type);

  /// @brief Calculate SHA256 hash of a file.
  /// @param file_path Path to the file.
  /// @return SHA256 hash as hex string, or empty string on error.
  std::string CalculateFileHash(const std::string& file_path);

  /// @brief Check if a file exists and is readable.
  /// @param file_path Path to the file.
  /// @return True if file exists and is readable, false otherwise.
  bool FileExists(const std::string& file_path);

  /// @brief Copy a file from source to destination.
  /// @param src_path Source file path.
  /// @param dest_path Destination file path.
  /// @return True on success, false on failure.
  bool CopyFile(const std::string& src_path, const std::string& dest_path);

  /// @brief Sync certificates for a single domain.
  /// @param domain_config Domain configuration.
  /// @return True on success or no sync needed, false on failure.
  bool SyncDomainCertificates(const DomainConfig& domain_config);

  /// @brief Sync a single certificate file.
  /// @param domain_config Domain configuration.
  /// @param cert_type Certificate type to sync.
  /// @return True on success or no sync needed, false on failure.
  bool SyncCertificateFile(const DomainConfig& domain_config,
                           CertType cert_type);

  /// @brief Create nginx ssl directory if it doesn't exist.
  /// @return True on success, false on failure.
  bool EnsureNginxSslDir();

  /// @brief The main loop that runs in the background thread.
  void UpdateLoop();

  // Threading and synchronization
  std::atomic<bool> running_;      ///< Whether the thread is running
  std::atomic<bool> should_stop_;  ///< Signal to stop the thread
  std::thread update_thread_;      ///< Background update thread
  mutable std::mutex mutex_;       ///< Mutex for thread synchronization
  std::condition_variable cv_;     ///< Condition variable for thread control
  mutable std::mutex init_mutex_;  ///< Mutex for initialization

  // State
  std::atomic<bool> initialized_;      ///< Whether manager is initialized
  std::vector<DomainConfig> domains_;  ///< Configured domains
  int check_interval_seconds_;         ///< Check interval in seconds
};

}  // namespace impl
}  // namespace tbox

#endif  // TBOX_IMPL_CERT_MANAGER_H_
