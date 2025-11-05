/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_CLIENT_SSL_CONFIG_MANAGER_H_
#define TBOX_CLIENT_SSL_CONFIG_MANAGER_H_

#include <sys/stat.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "folly/Singleton.h"
#include "glog/logging.h"
#include "grpcpp/grpcpp.h"
#include "src/proto/config.pb.h"
#include "src/proto/service.grpc.pb.h"
#include "src/proto/service.pb.h"

namespace tbox {
namespace client {

class SSLConfigManager {
 public:
  /// @brief Get singleton instance.
  /// @return Shared pointer to SSLConfigManager instance.
  static std::shared_ptr<SSLConfigManager> Instance();

  ~SSLConfigManager();

  /// @brief Initialize with gRPC channel.
  /// @param channel Shared pointer to gRPC channel.
  /// @note Thread-safe initialization.
  void Init(std::shared_ptr<grpc::Channel> channel);

  /// @brief Load CA certificate from file path (static utility)
  /// @param cert_path Path to certificate file
  /// @return Certificate content as string, empty if failed
  static std::string LoadCACert(const std::string& cert_path);

  /// @brief Start the SSL certificate monitoring thread
  void Start();

  /// @brief Stop the SSL certificate monitoring thread
  void Stop();

  /// @brief Check if the manager is running.
  /// @return True if the monitoring thread is active, false otherwise.
  bool IsRunning() const { return running_.load(); }

 private:
  friend class folly::Singleton<SSLConfigManager>;
  SSLConfigManager();

  // Monitor certificate changes every 5 seconds
  void MonitorCertificate();

  // Fetch new certificates from server
  bool FetchAndStoreCertificates();

  // Write certificate files to nginx ssl directory
  void WriteCertificateFiles(const std::string& cert_content,
                             const std::string& key_content,
                             const std::string& ca_content);

  // Read file content (static utility)
  static std::string ReadFileContent(const std::string& file_path);

  // Write file content
  bool WriteFileContent(const std::string& file_path,
                        const std::string& content);

  // Execute shell command and get output
  std::string ExecuteCommand(const std::string& command);

  // Set file permissions
  bool SetFilePermissions(const std::string& file_path, mode_t permissions);

  // Get complete certificate chain from remote server using openssl s_client
  std::string GetRemoteCertificateChain();

  // Get individual certificates from chain (server, intermediate, root)
  struct CertificateChain {
    std::string server_cert;
    std::string intermediate_cert;
    std::string root_cert;
    std::string fullchain;
  };
  CertificateChain ParseCertificateChain(const std::string& chain);

  // Compare certificate contents (not just fingerprints)
  bool AreCertificatesEqual(const std::string& cert1, const std::string& cert2);

  // Set directory ownership to www-data user
  bool SetWwwDataOwnership(const std::string& directory_path);

  // Check and update tbox certificate location
  bool UpdateTboxCertificate();

  // Check and update nginx SSL certificates
  bool UpdateNginxCertificates();

  // Validate that fullchain contains server + intermediate + root certs
  bool ValidateFullchainCertificate(const std::string& fullchain_path);

  // Get SHA256 hash of remote private key
  std::string GetRemotePrivateKeyHash();

  // Get SHA256 hash of local private key
  std::string GetLocalPrivateKeyHash(const std::string& key_path);

  // Fetch private key from server and store locally
  bool FetchAndStorePrivateKey(const std::string& key_path);

  // Check and update private key if needed
  bool UpdatePrivateKey(const std::string& key_path);

  // Get SHA256 hash of remote fullchain certificate
  std::string GetRemoteFullchainCertHash();

  // Get SHA256 hash of local fullchain certificate
  std::string GetLocalFullchainCertHash(const std::string& cert_path);

  // Fetch fullchain certificate from server and store locally
  bool FetchAndStoreFullchainCert(const std::string& cert_path);

  // Check and update fullchain certificate if needed
  bool UpdateFullchainCertificate(const std::string& cert_path);

  // Get SHA256 hash of remote CA certificate
  std::string GetRemoteCACertHash();

  // Get SHA256 hash of local CA certificate
  std::string GetLocalCACertHash(const std::string& cert_path);

  // Fetch CA certificate from server and store locally
  bool FetchAndStoreCACert(const std::string& cert_path);

  // Check and update CA certificate if needed
  bool UpdateCACertificate(const std::string& cert_path);

  std::atomic<bool> running_;
  std::unique_ptr<std::thread> monitor_thread_;
  std::shared_ptr<grpc::Channel> channel_;
  mutable std::mutex init_mutex_;

  // Certificate monitoring configuration
  static constexpr int kMonitorIntervalSeconds = 5;
};

}  // namespace client
}  // namespace tbox

#endif  // TBOX_CLIENT_SSL_CONFIG_MANAGER_H_