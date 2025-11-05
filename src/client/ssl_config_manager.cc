/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/client/ssl_config_manager.h"

#include <sys/stat.h>

#include <cstring>
#include <filesystem>
#include <mutex>
#include <sstream>

#include "glog/logging.h"

namespace tbox {
namespace client {

// Static member definitions
std::shared_ptr<SSLConfigManager> SSLConfigManager::instance_ = nullptr;
std::mutex SSLConfigManager::instance_mutex_;

std::shared_ptr<SSLConfigManager> SSLConfigManager::Instance() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (!instance_) {
    // For singleton, we need a default config. This will be updated when
    // needed.
    static proto::BaseConfig default_config;
    instance_ =
        std::shared_ptr<SSLConfigManager>(new SSLConfigManager(default_config));
  }
  return instance_;
}

SSLConfigManager::SSLConfigManager(const proto::BaseConfig& config)
    : config_(&config), running_(false) {
  LOG(INFO) << "SSL Config Manager initialized";
}

SSLConfigManager::~SSLConfigManager() {
  Stop();
}

void SSLConfigManager::UpdateConfig(const proto::BaseConfig& config) {
  config_ = &config;
  LOG(INFO) << "SSL Config Manager configuration updated";
}

std::string SSLConfigManager::LoadCACert(const std::string& cert_path) {
  return ReadFileContent(cert_path);
}

void SSLConfigManager::Start() {
  if (!config_ || !config_->update_certs()) {
    LOG(INFO) << "Certificate updates disabled in configuration";
    return;
  }

  if (running_.load()) {
    LOG(WARNING) << "SSL Config Manager is already running";
    return;
  }

  running_.store(true);
  monitor_thread_ = std::make_unique<std::thread>(
      &SSLConfigManager::MonitorCertificate, this);
  LOG(INFO) << "SSL Config Manager started";
}

void SSLConfigManager::Stop() {
  if (!running_.load()) {
    return;
  }

  running_.store(false);
  if (monitor_thread_ && monitor_thread_->joinable()) {
    monitor_thread_->join();
  }
  LOG(INFO) << "SSL Config Manager stopped";
}

void SSLConfigManager::MonitorCertificate() {
  while (running_.load()) {
    try {
      if (HasCertificateChanged()) {
        LOG(INFO) << "Certificate change detected, updating...";

        // Get current remote certificate and update local copy
        std::string remote_cert = GetRemoteCertificateFingerprint();
        if (!remote_cert.empty()) {
          UpdateLocalCertificate(remote_cert);

          // Fetch new certificates from server and store them
          if (FetchAndStoreCertificates()) {
            LOG(INFO) << "Certificates updated successfully";
          } else {
            LOG(ERROR) << "Failed to fetch and store certificates";
          }
        }
      }
    } catch (const std::exception& e) {
      LOG(ERROR) << "Error in certificate monitoring: " << e.what();
    }

    // Sleep for monitoring interval
    std::this_thread::sleep_for(std::chrono::seconds(kMonitorIntervalSeconds));
  }
}

bool SSLConfigManager::HasCertificateChanged() {
  std::string remote_fingerprint = GetRemoteCertificateFingerprint();
  std::string local_fingerprint = GetLocalCertificateFingerprint();

  if (remote_fingerprint.empty()) {
    LOG(WARNING) << "Could not get remote certificate fingerprint";
    return false;
  }

  bool changed = (remote_fingerprint != local_fingerprint);
  if (changed) {
    LOG(INFO) << "Certificate fingerprint changed. Remote: "
              << remote_fingerprint << ", Local: " << local_fingerprint;
  }

  return changed;
}

std::string SSLConfigManager::GetRemoteCertificateFingerprint() {
  if (!config_) {
    LOG(ERROR) << "Configuration not available";
    return "";
  }

  // Extract domain from server_addr (remove https:// prefix)
  std::string server_addr = config_->server_addr();
  std::string domain = server_addr;

  // Remove protocol prefix if present
  size_t protocol_pos = domain.find("://");
  if (protocol_pos != std::string::npos) {
    domain = domain.substr(protocol_pos + 3);
  }

  // Remove port if present
  size_t port_pos = domain.find(":");
  if (port_pos != std::string::npos) {
    domain = domain.substr(0, port_pos);
  }

  // Use openssl command to get certificate fingerprint from remote server
  std::string command =
      "echo | openssl s_client -connect " + domain + ":443 -servername " +
      domain + " 2>/dev/null | openssl x509 -fingerprint -noout -sha256";

  std::string result = ExecuteCommand(command);

  // Extract fingerprint from output (format: "SHA256 Fingerprint=XX:XX:...")
  size_t pos = result.find("SHA256 Fingerprint=");
  if (pos != std::string::npos) {
    return result.substr(pos + 19);  // 19 = length of "SHA256 Fingerprint="
  }

  return "";
}

std::string SSLConfigManager::GetLocalCertificateFingerprint() {
  if (!config_) {
    LOG(ERROR) << "Configuration not available";
    return "";
  }

  std::string local_cert_path = config_->local_cert_path();
  if (local_cert_path.empty()) {
    local_cert_path = "conf/xiedeacc.com.ca.cer";  // Default fallback
  }

  if (!std::filesystem::exists(local_cert_path)) {
    LOG(INFO) << "Local certificate file does not exist: " << local_cert_path;
    return "";
  }

  // Calculate fingerprint of local certificate file
  std::string command = "openssl x509 -in " + local_cert_path +
                        " -fingerprint -noout -sha256 2>/dev/null";

  std::string result = ExecuteCommand(command);

  // Extract fingerprint from output
  size_t pos = result.find("SHA256 Fingerprint=");
  if (pos != std::string::npos) {
    return result.substr(pos + 19);
  }

  return "";
}

void SSLConfigManager::UpdateLocalCertificate(const std::string& cert_content) {
  // For now, we just update the fingerprint. The actual certificate content
  // will be fetched from the cert handler
  LOG(INFO) << "Local certificate fingerprint updated: "
            << cert_content.substr(0, 20) << "...";
}

bool SSLConfigManager::FetchAndStoreCertificates() {
  try {
    if (!config_) {
      LOG(ERROR) << "Configuration not available";
      return false;
    }

    LOG(INFO) << "Fetching certificates from acme.sh directory";

    std::string nginx_ssl_path = config_->nginx_ssl_path();
    if (nginx_ssl_path.empty()) {
      nginx_ssl_path = "/etc/nginx/ssl";  // Default fallback
    }

    // Create nginx ssl directory if it doesn't exist
    std::filesystem::create_directories(nginx_ssl_path);

    // Read certificates from acme.sh directory
    const std::string acme_base_path = "/home/ubuntu/.acme.sh/xiedeacc.com_ecc";

    std::string fullchain_content =
        ReadFileContent(acme_base_path + "/fullchain.cer");
    std::string key_content =
        ReadFileContent(acme_base_path + "/xiedeacc.com.key");
    std::string ca_content = ReadFileContent(acme_base_path + "/ca.cer");

    if (fullchain_content.empty() || key_content.empty() ||
        ca_content.empty()) {
      LOG(ERROR) << "Failed to read certificate files from: " << acme_base_path;
      return false;
    }

    // Write certificates to nginx ssl directory with proper names
    WriteCertificateFiles(fullchain_content, key_content, ca_content);

    LOG(INFO) << "Certificates successfully copied to: " << nginx_ssl_path;

    return true;

  } catch (const std::exception& e) {
    LOG(ERROR) << "Error fetching certificates: " << e.what();
    return false;
  }
}

void SSLConfigManager::WriteCertificateFiles(const std::string& cert_content,
                                             const std::string& key_content,
                                             const std::string& ca_content) {
  try {
    if (!config_) {
      LOG(ERROR) << "Configuration not available";
      return;
    }

    std::string nginx_ssl_path = config_->nginx_ssl_path();
    if (nginx_ssl_path.empty()) {
      nginx_ssl_path = "/etc/nginx/ssl";  // Default fallback
    }

    std::filesystem::create_directories(nginx_ssl_path);

    // Write files with names matching nginx configuration
    WriteFileContent(nginx_ssl_path + "/xiedeacc.com.fullchain.cer",
                     cert_content);
    WriteFileContent(nginx_ssl_path + "/xiedeacc.com.key", key_content);
    WriteFileContent(nginx_ssl_path + "/xiedeacc.com.ca.cer", ca_content);

    // Set proper file permissions
    SetFilePermissions(nginx_ssl_path + "/xiedeacc.com.fullchain.cer", 0644);
    SetFilePermissions(nginx_ssl_path + "/xiedeacc.com.key",
                       0600);  // Private key more restrictive
    SetFilePermissions(nginx_ssl_path + "/xiedeacc.com.ca.cer", 0644);

    LOG(INFO) << "Certificate files written to " << nginx_ssl_path;
  } catch (const std::exception& e) {
    LOG(ERROR) << "Error writing certificate files: " << e.what();
  }
}

std::string SSLConfigManager::ReadFileContent(const std::string& file_path) {
  std::ifstream file(file_path);
  if (!file.is_open()) {
    return "";
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

bool SSLConfigManager::WriteFileContent(const std::string& file_path,
                                        const std::string& content) {
  std::ofstream file(file_path);
  if (!file.is_open()) {
    LOG(ERROR) << "Failed to open file for writing: " << file_path;
    return false;
  }

  file << content;
  file.close();
  return true;
}

std::string SSLConfigManager::ExecuteCommand(const std::string& command) {
  FILE* pipe = popen(command.c_str(), "r");
  if (!pipe) {
    return "";
  }

  char buffer[256];
  std::string result;
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    result += buffer;
  }

  pclose(pipe);

  // Remove trailing newline
  if (!result.empty() && result.back() == '\n') {
    result.pop_back();
  }

  return result;
}

bool SSLConfigManager::SetFilePermissions(const std::string& file_path,
                                          mode_t permissions) {
  if (chmod(file_path.c_str(), permissions) != 0) {
    LOG(ERROR) << "Failed to set permissions for file: " << file_path
               << ", error: " << strerror(errno);
    return false;
  }
  return true;
}

}  // namespace client
}  // namespace tbox