/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/client/ssl_config_manager.h"

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>

#include <cstring>
#include <filesystem>
#include <mutex>
#include <sstream>

#include "glog/logging.h"
#include "src/async_grpc/client.h"
#include "src/client/authentication_manager.h"
#include "src/impl/config_manager.h"
#include "src/server/grpc_handler/meta.h"

namespace tbox {
namespace client {

static folly::Singleton<SSLConfigManager> ssl_config_manager;

std::shared_ptr<SSLConfigManager> SSLConfigManager::Instance() {
  return ssl_config_manager.try_get();
}

SSLConfigManager::SSLConfigManager() : running_(false) {}

SSLConfigManager::~SSLConfigManager() {
  Stop();
}

void SSLConfigManager::Init(std::shared_ptr<grpc::Channel> channel) {
  std::lock_guard<std::mutex> lock(init_mutex_);
  channel_ = channel;
  LOG(INFO) << "SSL Config Manager initialized";
}

std::string SSLConfigManager::LoadCACert(const std::string& cert_path) {
  return ReadFileContent(cert_path);
}

void SSLConfigManager::Start() {
  auto config = util::ConfigManager::Instance();
  if (!config->UpdateCerts()) {
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
  // Wait a bit before starting to ensure gRPC client is fully initialized
  LOG(INFO) << "SSL config manager starting, waiting 5 seconds for system "
               "initialization...";
  std::this_thread::sleep_for(std::chrono::seconds(5));

  int consecutive_failures = 0;
  const int max_backoff_interval = 300;  // 5 minutes maximum

  while (running_.load()) {
    try {
      // Only proceed if we have a valid channel
      if (!channel_) {
        LOG(WARNING)
            << "gRPC channel not available, skipping certificate update";
        std::this_thread::sleep_for(
            std::chrono::seconds(kMonitorIntervalSeconds));
        continue;
      }

      // Check and update tbox certificate
      bool tbox_updated = UpdateTboxCertificate();

      // Check and update nginx certificates
      bool nginx_updated = UpdateNginxCertificates();

      if (tbox_updated || nginx_updated) {
        LOG(INFO) << "Certificate update completed - tbox: "
                  << (tbox_updated ? "updated" : "unchanged")
                  << ", nginx: " << (nginx_updated ? "updated" : "unchanged");
        consecutive_failures = 0;  // Reset failure count on success
      } else {
        // If no updates happened, it might be due to server unavailability
        // Only increment failures if we couldn't reach the server at all
        if (!tbox_updated && !nginx_updated) {
          consecutive_failures++;
        }
      }
    } catch (const std::exception& e) {
      LOG(ERROR) << "Error in certificate monitoring: " << e.what();
      consecutive_failures++;
    }

    // Implement exponential backoff for failed attempts
    int sleep_interval = kMonitorIntervalSeconds;
    if (consecutive_failures > 0) {
      sleep_interval =
          std::min(kMonitorIntervalSeconds *
                       (1 << std::min(consecutive_failures - 1, 6)),
                   max_backoff_interval);
      if (consecutive_failures == 1) {
        LOG(INFO) << "Certificate server appears unavailable, reducing check "
                     "frequency";
      }
    }

    std::this_thread::sleep_for(std::chrono::seconds(sleep_interval));
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
  auto config = util::ConfigManager::Instance();

  // Extract domain from server_addr (remove https:// prefix)
  std::string server_addr = config->ServerAddr();
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
  auto config = util::ConfigManager::Instance();

  std::string local_cert_path = config->LocalCertPath();
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
    auto config = util::ConfigManager::Instance();

    LOG(INFO) << "Fetching certificates from acme.sh directory";

    std::string nginx_ssl_path = config->NginxSslPath();
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
    auto config = util::ConfigManager::Instance();

    std::string nginx_ssl_path = config->NginxSslPath();
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
  // Ensure parent directory exists
  std::filesystem::path file_path_obj(file_path);
  std::filesystem::path parent_dir = file_path_obj.parent_path();

  if (!parent_dir.empty() && !std::filesystem::exists(parent_dir)) {
    LOG(INFO) << "Creating parent directory: " << parent_dir;
    std::error_code ec;
    if (!std::filesystem::create_directories(parent_dir, ec)) {
      LOG(ERROR) << "Failed to create parent directory " << parent_dir << ": "
                 << ec.message();
      return false;
    }
  }

  std::ofstream file(file_path);
  if (!file.is_open()) {
    LOG(ERROR) << "Failed to open file for writing: " << file_path
               << " (errno: " << errno << " - " << strerror(errno) << ")";
    return false;
  }

  file << content;
  if (file.fail()) {
    LOG(ERROR) << "Failed to write content to file: " << file_path
               << " (errno: " << errno << " - " << strerror(errno) << ")";
    file.close();
    return false;
  }

  file.close();
  if (file.fail()) {
    LOG(ERROR) << "Failed to close file: " << file_path << " (errno: " << errno
               << " - " << strerror(errno) << ")";
    return false;
  }

  LOG(INFO) << "Successfully wrote " << content.length()
            << " bytes to: " << file_path;
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

std::string SSLConfigManager::GetRemoteCertificateChain() {
  // Additional safety check for channel
  if (!channel_) {
    LOG(WARNING) << "gRPC channel not available for SSL config manager";
    return "";
  }

  auto auth_manager = client::AuthenticationManager::Instance();
  if (!auth_manager) {
    LOG(ERROR) << "Authentication manager not available";
    return "";
  }

  if (!auth_manager->IsAuthenticated()) {
    LOG(WARNING) << "Not authenticated, cannot get remote certificate chain";
    return "";
  }

  try {
    async_grpc::Client<server::grpc_handler::CertOpMethod> client(channel_);

    tbox::proto::CertRequest request;
    request.set_request_id(util::Util::UUID());
    request.set_op(tbox::proto::OpCode::OP_GET_FULLCHAIN_CERT);
    request.set_token(auth_manager->GetToken());

    // Use async_grpc client like report_manager does
    grpc::Status status;
    bool success = client.Write(request, &status);

    if (success && status.ok()) {
      const auto& response = client.response();
      if (response.err_code() == tbox::proto::ErrCode::Success) {
        // Fullchain certificate content should be in certificate field
        std::string cert_content = response.certificate();

        if (!cert_content.empty()) {
          LOG(INFO) << "Successfully retrieved fullchain certificate chain "
                       "from server";
          return cert_content;
        } else {
          LOG(ERROR)
              << "Empty fullchain certificate content received from server";
        }
      } else {
        LOG(WARNING) << "Failed to fetch fullchain certificate chain from "
                        "server - gRPC status: "
                     << (status.ok() ? "OK" : "FAILED")
                     << ", error: " << status.error_message()
                     << ", response error code: "
                     << static_cast<int>(response.err_code());
      }
    } else {
      LOG(WARNING) << "Failed to fetch fullchain certificate chain from server "
                      "- gRPC status: "
                   << (status.ok() ? "OK" : "FAILED")
                   << ", error: " << status.error_message();
    }
  } catch (const std::exception& e) {
    LOG(ERROR) << "Exception fetching remote certificate chain: " << e.what();
  }

  return "";
}

SSLConfigManager::CertificateChain SSLConfigManager::ParseCertificateChain(
    const std::string& chain) {
  CertificateChain cert_chain;

  // Find all certificate blocks in the chain
  std::vector<std::string> certificates;
  size_t pos = 0;

  while (true) {
    size_t begin_pos = chain.find("-----BEGIN CERTIFICATE-----", pos);
    if (begin_pos == std::string::npos)
      break;

    size_t end_pos = chain.find("-----END CERTIFICATE-----", begin_pos);
    if (end_pos == std::string::npos)
      break;

    end_pos += 25;  // Include "-----END CERTIFICATE-----"
    std::string cert = chain.substr(begin_pos, end_pos - begin_pos);
    certificates.push_back(cert);
    pos = end_pos;
  }

  // First certificate is typically the server certificate
  if (!certificates.empty()) {
    cert_chain.server_cert = certificates[0];
  }

  // Second certificate is typically the intermediate CA
  if (certificates.size() > 1) {
    cert_chain.intermediate_cert = certificates[1];
  }

  // Last certificate is typically the root CA
  if (certificates.size() > 2) {
    cert_chain.root_cert = certificates.back();
  }

  // Build fullchain (server + intermediate + root)
  cert_chain.fullchain = "";
  for (const auto& cert : certificates) {
    cert_chain.fullchain += cert + "\n";
  }

  return cert_chain;
}

bool SSLConfigManager::AreCertificatesEqual(const std::string& cert1,
                                            const std::string& cert2) {
  if (cert1.empty() || cert2.empty()) {
    return cert1.empty() && cert2.empty();
  }

  // Normalize whitespace and compare certificate content
  auto normalize = [](const std::string& cert) {
    std::string normalized;
    std::istringstream iss(cert);
    std::string line;

    while (std::getline(iss, line)) {
      // Trim whitespace
      line.erase(0, line.find_first_not_of(" \t\r\n"));
      line.erase(line.find_last_not_of(" \t\r\n") + 1);

      if (!line.empty()) {
        normalized += line + "\n";
      }
    }
    return normalized;
  };

  return normalize(cert1) == normalize(cert2);
}

bool SSLConfigManager::SetWwwDataOwnership(const std::string& directory_path) {
  // Get www-data user and group IDs
  struct passwd* pwd = getpwnam("www-data");
  struct group* grp = getgrnam("www-data");

  if (!pwd || !grp) {
    LOG(WARNING) << "www-data user/group not found, using root ownership";
    return true;  // Not a critical error
  }

  // Set ownership of directory
  if (chown(directory_path.c_str(), pwd->pw_uid, grp->gr_gid) != 0) {
    LOG(WARNING) << "Failed to set www-data ownership for directory: "
                 << directory_path << ", error: " << strerror(errno);
    return false;
  }

  // Set ownership for all files in directory
  try {
    for (const auto& entry :
         std::filesystem::directory_iterator(directory_path)) {
      if (entry.is_regular_file()) {
        std::string file_path = entry.path().string();
        if (chown(file_path.c_str(), pwd->pw_uid, grp->gr_gid) != 0) {
          LOG(WARNING) << "Failed to set www-data ownership for file: "
                       << file_path << ", error: " << strerror(errno);
        }
      }
    }
  } catch (const std::exception& e) {
    LOG(WARNING) << "Error setting ownership for files in " << directory_path
                 << ": " << e.what();
    return false;
  }

  return true;
}

bool SSLConfigManager::UpdateTboxCertificate() {
  // Get remote certificate chain
  std::string remote_chain = GetRemoteCertificateChain();
  if (remote_chain.empty()) {
    LOG(WARNING) << "Failed to get remote certificate chain";
    return false;
  }

  CertificateChain cert_chain = ParseCertificateChain(remote_chain);
  if (cert_chain.root_cert.empty()) {
    LOG(WARNING) << "No root certificate found in remote chain";
    return false;
  }

  // Check tbox certificate location: /usr/local/tbox/conf/xiedeacc.com.ca.cer
  std::string tbox_cert_path = "/usr/local/tbox/conf/xiedeacc.com.ca.cer";
  std::string local_cert = ReadFileContent(tbox_cert_path);

  if (!AreCertificatesEqual(local_cert, cert_chain.root_cert)) {
    LOG(INFO) << "Tbox certificate differs from remote, updating...";

    // Create directory if it doesn't exist
    std::filesystem::create_directories("/usr/local/tbox/conf");

    // Write new certificate
    if (WriteFileContent(tbox_cert_path, cert_chain.root_cert)) {
      SetFilePermissions(tbox_cert_path, 0644);
      LOG(INFO) << "Tbox certificate updated: " << tbox_cert_path;
      return true;
    } else {
      LOG(ERROR) << "Failed to write tbox certificate: " << tbox_cert_path;
      return false;
    }
  }

  return false;  // No update needed
}

bool SSLConfigManager::UpdateNginxCertificates() {
  auto config = util::ConfigManager::Instance();

  std::string nginx_ssl_path = config->NginxSslPath();
  if (nginx_ssl_path.empty()) {
    nginx_ssl_path = "/etc/nginx/ssl";
  }

  // Create nginx SSL directory if it doesn't exist
  if (!std::filesystem::exists(nginx_ssl_path)) {
    std::filesystem::create_directories(nginx_ssl_path);
    SetWwwDataOwnership(nginx_ssl_path);
  }

  bool updated = false;

  // Check required nginx certificate files
  std::string ca_cert_path = nginx_ssl_path + "/xiedeacc.com.ca.cer";
  std::string fullchain_path = nginx_ssl_path + "/xiedeacc.com.fullchain.cer";
  std::string key_path = nginx_ssl_path + "/xiedeacc.com.key";

  // Update CA certificate using hash comparison
  bool ca_updated = UpdateCACertificate(ca_cert_path);
  if (ca_updated) {
    updated = true;
  }

  // Update fullchain certificate using hash comparison
  bool fullchain_updated = UpdateFullchainCertificate(fullchain_path);
  if (fullchain_updated) {
    updated = true;
  }

  // Check and update private key
  bool key_updated = UpdatePrivateKey(key_path);
  if (key_updated) {
    updated = true;
    LOG(INFO) << "Private key updated: " << key_path;
  }

  // Set proper ownership for nginx SSL directory
  if (updated) {
    SetWwwDataOwnership(nginx_ssl_path);
    LOG(INFO) << "Nginx certificates updated and ownership set to www-data";
  }

  return updated;
}

bool SSLConfigManager::ValidateFullchainCertificate(
    const std::string& fullchain_path) {
  if (!std::filesystem::exists(fullchain_path)) {
    LOG(WARNING) << "Fullchain certificate file does not exist: "
                 << fullchain_path;
    return false;
  }

  std::string fullchain_content = ReadFileContent(fullchain_path);
  if (fullchain_content.empty()) {
    LOG(ERROR) << "Failed to read fullchain certificate: " << fullchain_path;
    return false;
  }

  CertificateChain cert_chain = ParseCertificateChain(fullchain_content);

  // Validate that we have at least server certificate
  if (cert_chain.server_cert.empty()) {
    LOG(ERROR) << "No server certificate found in fullchain: "
               << fullchain_path;
    return false;
  }

  // Log certificate chain information
  int cert_count = 0;
  if (!cert_chain.server_cert.empty())
    cert_count++;
  if (!cert_chain.intermediate_cert.empty())
    cert_count++;
  if (!cert_chain.root_cert.empty())
    cert_count++;

  LOG(INFO) << "Fullchain certificate validation for " << fullchain_path
            << " - found " << cert_count << " certificate(s)";

  return cert_count >= 1;  // At minimum we need a server certificate
}

std::string SSLConfigManager::GetRemotePrivateKeyHash() {
  // Additional safety check for channel
  if (!channel_) {
    LOG(WARNING) << "gRPC channel not available for SSL config manager";
    return "";
  }

  auto auth_manager = client::AuthenticationManager::Instance();
  if (!auth_manager) {
    LOG(ERROR) << "Authentication manager not available";
    return "";
  }

  if (!auth_manager->IsAuthenticated()) {
    LOG(WARNING) << "Not authenticated, cannot get remote private key hash";
    return "";
  }

  try {
    async_grpc::Client<server::grpc_handler::CertOpMethod> client(channel_);

    tbox::proto::CertRequest request;
    request.set_request_id(util::Util::UUID());
    request.set_op(tbox::proto::OpCode::OP_GET_PRIVATE_KEY_HASH);
    request.set_token(auth_manager->GetToken());

    // Use async_grpc client like report_manager does
    grpc::Status status;
    bool success = client.Write(request, &status);

    if (success && status.ok()) {
      const auto& response = client.response();
      if (response.err_code() == tbox::proto::ErrCode::Success) {
        // Private key hash should be in the message field
        if (!response.message().empty()) {
          return response.message();
        } else {
          LOG(WARNING)
              << "Remote private key hash response empty - no message field";
        }
      } else {
        LOG(INFO) << "Failed to get remote private key hash - gRPC status: "
                  << (status.ok() ? "OK" : "FAILED")
                  << ", error: " << status.error_message()
                  << ", response error code: "
                  << static_cast<int>(response.err_code());
      }
    } else {
      LOG(INFO) << "Failed to get remote private key hash - gRPC status: "
                << (status.ok() ? "OK" : "FAILED")
                << ", error: " << status.error_message();
    }
  } catch (const std::exception& e) {
    LOG(ERROR) << "Exception getting remote private key hash: " << e.what();
  }

  return "";
}

std::string SSLConfigManager::GetLocalPrivateKeyHash(
    const std::string& key_path) {
  if (!std::filesystem::exists(key_path)) {
    LOG(INFO) << "Local private key does not exist: " << key_path;
    return "";
  }

  // Calculate SHA256 hash of the private key file
  std::string result;
  bool success = util::Util::FileSHA256(key_path, &result);

  if (!success || result.empty()) {
    LOG(WARNING) << "Failed to calculate hash for local private key: "
                 << key_path;
    return "";
  } else {
    LOG(INFO) << "Local private key hash: " << result.substr(0, 16) << "...";
  }

  return result;
}

bool SSLConfigManager::FetchAndStorePrivateKey(const std::string& key_path) {
  // Additional safety check for channel
  if (!channel_) {
    LOG(WARNING) << "gRPC channel not available for SSL config manager";
    return false;
  }

  auto auth_manager = client::AuthenticationManager::Instance();
  if (!auth_manager->IsAuthenticated()) {
    LOG(WARNING) << "Not authenticated, cannot fetch private key";
    return false;
  }

  try {
    async_grpc::Client<server::grpc_handler::CertOpMethod> client(channel_);

    tbox::proto::CertRequest request;
    request.set_request_id(util::Util::UUID());
    request.set_op(tbox::proto::OpCode::OP_GET_PRIVATE_KEY);
    request.set_token(auth_manager->GetToken());

    // Use async_grpc client like report_manager does
    grpc::Status status;
    bool success = client.Write(request, &status);

    if (success && status.ok()) {
      const auto& response = client.response();
      if (response.err_code() == tbox::proto::ErrCode::Success) {
        // Private key content should be in private_key field
        std::string private_key_content = response.private_key();

        if (!private_key_content.empty()) {
          // Write private key to file
          if (WriteFileContent(key_path, private_key_content)) {
            // Set restrictive permissions for private key (600 = rw-------)
            SetFilePermissions(key_path, 0600);
            LOG(INFO) << "Successfully fetched and stored private key: "
                      << key_path;
            return true;
          } else {
            LOG(ERROR) << "Failed to write private key to: " << key_path;
          }
        } else {
          LOG(ERROR) << "Empty private key content received from server";
        }
      } else {
        LOG(WARNING)
            << "Failed to fetch private key from server - gRPC status: "
            << (status.ok() ? "OK" : "FAILED")
            << ", error: " << status.error_message()
            << ", response error code: "
            << static_cast<int>(response.err_code());
      }
    } else {
      LOG(WARNING) << "Failed to fetch private key from server - gRPC status: "
                   << (status.ok() ? "OK" : "FAILED")
                   << ", error: " << status.error_message();
    }
  } catch (const std::exception& e) {
    LOG(ERROR) << "Exception fetching private key: " << e.what();
  }

  return false;
}

bool SSLConfigManager::UpdatePrivateKey(const std::string& key_path) {
  // Get remote private key hash
  std::string remote_hash = GetRemotePrivateKeyHash();
  if (remote_hash.empty()) {
    LOG(INFO)
        << "Could not get remote private key hash (server may be unavailable)";
    return false;
  }

  // Get local private key hash
  std::string local_hash = GetLocalPrivateKeyHash(key_path);

  // Compare hashes
  if (local_hash != remote_hash) {
    LOG(INFO) << "Private key differs from server (local: "
              << (local_hash.empty() ? "missing"
                                     : local_hash.substr(0, 16) + "...")
              << ", remote: "
              << remote_hash.substr(0, 16) + "...), updating...";

    // Fetch and store the updated private key
    return FetchAndStorePrivateKey(key_path);
  } else {
    LOG(INFO) << "Private key is up to date";
    return false;  // No update needed
  }
}

std::string SSLConfigManager::GetRemoteFullchainCertHash() {
  // Additional safety check for channel
  if (!channel_) {
    LOG(WARNING) << "gRPC channel not available for SSL config manager";
    return "";
  }

  auto auth_manager = client::AuthenticationManager::Instance();
  if (!auth_manager) {
    LOG(ERROR) << "Authentication manager not available";
    return "";
  }

  if (!auth_manager->IsAuthenticated()) {
    LOG(WARNING)
        << "Not authenticated, cannot get remote fullchain certificate hash";
    return "";
  }

  try {
    async_grpc::Client<server::grpc_handler::CertOpMethod> client(channel_);

    tbox::proto::CertRequest request;
    request.set_request_id(util::Util::UUID());
    request.set_op(tbox::proto::OpCode::OP_GET_FULLCHAIN_CERT_HASH);
    request.set_token(auth_manager->GetToken());

    // Use async_grpc client like report_manager does
    grpc::Status status;
    bool success = client.Write(request, &status);

    if (success && status.ok()) {
      const auto& response = client.response();
      if (response.err_code() == tbox::proto::ErrCode::Success) {
        // Fullchain certificate hash should be in the message field
        if (!response.message().empty()) {
          return response.message();
        } else {
          LOG(WARNING) << "Remote fullchain certificate hash response empty - "
                          "no message field";
        }
      } else {
        LOG(INFO)
            << "Failed to get remote fullchain certificate hash - gRPC status: "
            << (status.ok() ? "OK" : "FAILED")
            << ", error: " << status.error_message()
            << ", response error code: "
            << static_cast<int>(response.err_code());
      }
    } else {
      LOG(INFO)
          << "Failed to get remote fullchain certificate hash - gRPC status: "
          << (status.ok() ? "OK" : "FAILED")
          << ", error: " << status.error_message();
    }
  } catch (const std::exception& e) {
    LOG(ERROR) << "Exception getting remote fullchain certificate hash: "
               << e.what();
  }

  return "";
}

std::string SSLConfigManager::GetLocalFullchainCertHash(
    const std::string& cert_path) {
  if (!std::filesystem::exists(cert_path)) {
    return "";  // File doesn't exist, return empty hash
  }

  std::string result;
  bool success = util::Util::FileSHA256(cert_path, &result);

  if (!success) {
    LOG(WARNING) << "Failed to calculate hash for fullchain certificate: "
                 << cert_path;
    return "";
  }

  return result;
}

bool SSLConfigManager::FetchAndStoreFullchainCert(
    const std::string& cert_path) {
  // Additional safety check for channel
  if (!channel_) {
    LOG(WARNING) << "gRPC channel not available for SSL config manager";
    return false;
  }

  auto auth_manager = client::AuthenticationManager::Instance();
  if (!auth_manager) {
    LOG(ERROR) << "Authentication manager not available";
    return false;
  }

  if (!auth_manager->IsAuthenticated()) {
    LOG(WARNING) << "Not authenticated, cannot fetch fullchain certificate";
    return false;
  }

  try {
    async_grpc::Client<server::grpc_handler::CertOpMethod> client(channel_);

    tbox::proto::CertRequest request;
    request.set_request_id(util::Util::UUID());
    request.set_op(tbox::proto::OpCode::OP_GET_FULLCHAIN_CERT);
    request.set_token(auth_manager->GetToken());

    // Use async_grpc client like report_manager does
    grpc::Status status;
    bool success = client.Write(request, &status);

    if (success && status.ok()) {
      const auto& response = client.response();
      if (response.err_code() == tbox::proto::ErrCode::Success) {
        // Fullchain certificate content should be in certificate field
        std::string cert_content = response.certificate();

        if (!cert_content.empty()) {
          // Write fullchain certificate to file
          if (WriteFileContent(cert_path, cert_content)) {
            // Set read permissions for certificate (644 = rw-r--r--)
            SetFilePermissions(cert_path, 0644);
            LOG(INFO) << "Successfully wrote " << cert_content.length()
                      << " bytes to: " << cert_path;
            return true;
          } else {
            LOG(ERROR) << "Failed to write fullchain certificate to: "
                       << cert_path;
          }
        } else {
          LOG(ERROR)
              << "Empty fullchain certificate content received from server";
        }
      } else {
        LOG(WARNING) << "Failed to fetch fullchain certificate from server - "
                        "gRPC status: "
                     << (status.ok() ? "OK" : "FAILED")
                     << ", error: " << status.error_message()
                     << ", response error code: "
                     << static_cast<int>(response.err_code());
      }
    }
  } catch (const std::exception& e) {
    LOG(ERROR) << "Exception fetching fullchain certificate: " << e.what();
  }

  return false;
}

bool SSLConfigManager::UpdateFullchainCertificate(
    const std::string& cert_path) {
  // Get remote fullchain certificate hash
  std::string remote_hash = GetRemoteFullchainCertHash();
  if (remote_hash.empty()) {
    LOG(INFO) << "Could not get remote fullchain certificate hash (server may "
                 "be unavailable)";
    return false;
  }

  // Get local fullchain certificate hash
  std::string local_hash = GetLocalFullchainCertHash(cert_path);

  // Compare hashes
  if (local_hash != remote_hash) {
    LOG(INFO) << "Fullchain certificate differs from server (local: "
              << (local_hash.empty() ? "missing"
                                     : local_hash.substr(0, 16) + "...")
              << ", remote: "
              << remote_hash.substr(0, 16) + "...), updating...";

    // Fetch and store the updated fullchain certificate
    return FetchAndStoreFullchainCert(cert_path);
  } else {
    LOG(INFO) << "Fullchain certificate is up to date";
    return false;  // No update needed
  }
}

std::string SSLConfigManager::GetRemoteCACertHash() {
  // Additional safety check for channel
  if (!channel_) {
    LOG(WARNING) << "gRPC channel not available for SSL config manager";
    return "";
  }

  auto auth_manager = client::AuthenticationManager::Instance();
  if (!auth_manager) {
    LOG(ERROR) << "Authentication manager not available";
    return "";
  }

  if (!auth_manager->IsAuthenticated()) {
    LOG(WARNING) << "Not authenticated, cannot get remote CA certificate hash";
    return "";
  }

  try {
    async_grpc::Client<server::grpc_handler::CertOpMethod> client(channel_);

    tbox::proto::CertRequest request;
    request.set_request_id(util::Util::UUID());
    request.set_op(tbox::proto::OpCode::OP_GET_CA_CERT_HASH);
    request.set_token(auth_manager->GetToken());

    // Use async_grpc client like report_manager does
    grpc::Status status;
    bool success = client.Write(request, &status);

    if (success && status.ok()) {
      const auto& response = client.response();
      if (response.err_code() == tbox::proto::ErrCode::Success) {
        // CA certificate hash should be in the message field
        if (!response.message().empty()) {
          return response.message();
        } else {
          LOG(WARNING)
              << "Remote CA certificate hash response empty - no message field";
        }
      } else {
        LOG(INFO) << "Failed to get remote CA certificate hash - gRPC status: "
                  << (status.ok() ? "OK" : "FAILED")
                  << ", error: " << status.error_message()
                  << ", response error code: "
                  << static_cast<int>(response.err_code());
      }
    } else {
      LOG(INFO) << "Failed to get remote CA certificate hash - gRPC status: "
                << (status.ok() ? "OK" : "FAILED")
                << ", error: " << status.error_message();
    }
  } catch (const std::exception& e) {
    LOG(ERROR) << "Exception getting remote CA certificate hash: " << e.what();
  }

  return "";
}

std::string SSLConfigManager::GetLocalCACertHash(const std::string& cert_path) {
  if (!std::filesystem::exists(cert_path)) {
    return "";  // File doesn't exist, return empty hash
  }

  std::string result;
  bool success = util::Util::FileSHA256(cert_path, &result);

  if (!success) {
    LOG(WARNING) << "Failed to calculate hash for CA certificate: "
                 << cert_path;
    return "";
  }

  return result;
}

bool SSLConfigManager::FetchAndStoreCACert(const std::string& cert_path) {
  // Additional safety check for channel
  if (!channel_) {
    LOG(WARNING) << "gRPC channel not available for SSL config manager";
    return false;
  }

  auto auth_manager = client::AuthenticationManager::Instance();
  if (!auth_manager) {
    LOG(ERROR) << "Authentication manager not available";
    return false;
  }

  if (!auth_manager->IsAuthenticated()) {
    LOG(WARNING) << "Not authenticated, cannot fetch CA certificate";
    return false;
  }

  try {
    async_grpc::Client<server::grpc_handler::CertOpMethod> client(channel_);

    tbox::proto::CertRequest request;
    request.set_request_id(util::Util::UUID());
    request.set_op(tbox::proto::OpCode::OP_GET_CA_CERT);
    request.set_token(auth_manager->GetToken());

    // Use async_grpc client like report_manager does
    grpc::Status status;
    bool success = client.Write(request, &status);

    if (success && status.ok()) {
      const auto& response = client.response();
      if (response.err_code() == tbox::proto::ErrCode::Success) {
        // CA certificate content should be in ca_certificate field
        std::string cert_content = response.ca_certificate();

        if (!cert_content.empty()) {
          // Write CA certificate to file
          if (WriteFileContent(cert_path, cert_content)) {
            // Set read permissions for certificate (644 = rw-r--r--)
            SetFilePermissions(cert_path, 0644);
            LOG(INFO) << "Successfully wrote " << cert_content.length()
                      << " bytes to: " << cert_path;
            return true;
          } else {
            LOG(ERROR) << "Failed to write CA certificate to: " << cert_path;
          }
        } else {
          LOG(ERROR) << "Empty CA certificate content received from server";
        }
      } else {
        LOG(WARNING)
            << "Failed to fetch CA certificate from server - gRPC status: "
            << (status.ok() ? "OK" : "FAILED")
            << ", error: " << status.error_message()
            << ", response error code: "
            << static_cast<int>(response.err_code());
      }
    }
  } catch (const std::exception& e) {
    LOG(ERROR) << "Exception fetching CA certificate: " << e.what();
  }

  return false;
}

bool SSLConfigManager::UpdateCACertificate(const std::string& cert_path) {
  // Get remote CA certificate hash
  std::string remote_hash = GetRemoteCACertHash();
  if (remote_hash.empty()) {
    LOG(INFO) << "Could not get remote CA certificate hash (server may be "
                 "unavailable)";
    return false;
  }

  // Get local CA certificate hash
  std::string local_hash = GetLocalCACertHash(cert_path);

  // Compare hashes
  if (local_hash != remote_hash) {
    LOG(INFO) << "CA certificate differs from server (local: "
              << (local_hash.empty() ? "missing"
                                     : local_hash.substr(0, 16) + "...")
              << ", remote: "
              << remote_hash.substr(0, 16) + "...), updating...";

    // Fetch and store the updated CA certificate
    return FetchAndStoreCACert(cert_path);
  } else {
    LOG(INFO) << "CA certificate is up to date";
    return false;  // No update needed
  }
}

}  // namespace client
}  // namespace tbox