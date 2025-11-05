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

#include "glog/logging.h"
#include "src/proto/config.pb.h"
#include "src/proto/service.pb.h"

namespace tbox {
namespace client {

class SSLConfigManager {
 public:
  static std::shared_ptr<SSLConfigManager> Instance();

  explicit SSLConfigManager(const proto::BaseConfig& config);
  ~SSLConfigManager();

  // Update configuration
  void UpdateConfig(const proto::BaseConfig& config);

  // Load CA certificate from file path
  std::string LoadCACert(const std::string& cert_path);

  // Start the SSL certificate monitoring thread
  void Start();

  // Stop the SSL certificate monitoring thread
  void Stop();

 private:
  // Monitor certificate changes every 5 seconds
  void MonitorCertificate();

  // Check if remote certificate has changed
  bool HasCertificateChanged();

  // Get remote certificate information
  std::string GetRemoteCertificateFingerprint();

  // Read local certificate fingerprint
  std::string GetLocalCertificateFingerprint();

  // Update local certificate file
  void UpdateLocalCertificate(const std::string& cert_content);

  // Fetch new certificates from server
  bool FetchAndStoreCertificates();

  // Write certificate files to nginx ssl directory
  void WriteCertificateFiles(const std::string& cert_content,
                             const std::string& key_content,
                             const std::string& ca_content);

  // Read file content
  std::string ReadFileContent(const std::string& file_path);

  // Write file content
  bool WriteFileContent(const std::string& file_path,
                        const std::string& content);

  // Execute shell command and get output
  std::string ExecuteCommand(const std::string& command);

  // Set file permissions
  bool SetFilePermissions(const std::string& file_path, mode_t permissions);

  static std::shared_ptr<SSLConfigManager> instance_;
  static std::mutex instance_mutex_;

  const proto::BaseConfig* config_;
  std::atomic<bool> running_;
  std::unique_ptr<std::thread> monitor_thread_;

  // Certificate monitoring configuration
  static constexpr int kMonitorIntervalSeconds = 5;
};

}  // namespace client
}  // namespace tbox

#endif  // TBOX_CLIENT_SSL_CONFIG_MANAGER_H_