/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/impl/cert_manager.h"

#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#include "glog/logging.h"
#include "src/util/util.h"

namespace tbox {
namespace impl {

static folly::Singleton<CertManager> cert_manager;

std::shared_ptr<CertManager> CertManager::Instance() {
  return cert_manager.try_get();
}

CertManager::CertManager()
    : running_(false),
      should_stop_(false),
      initialized_(false),
      check_interval_seconds_(kCheckIntervalSeconds) {}

bool CertManager::Init() {
  std::lock_guard<std::mutex> lock(init_mutex_);

  if (initialized_) {
    LOG(WARNING) << "CertManager already initialized";
    return true;
  }

  LOG(INFO) << "Initializing CertManager...";

  // Configure supported domains
  domains_ = {{.domain = "xiedeacc.com",
               .acme_dir = "/home/ubuntu/.acme.sh/xiedeacc.com_ecc",
               .nginx_prefix = "xiedeacc.com"},
              {.domain = "youkechat.net",
               .acme_dir = "/home/ubuntu/.acme.sh/youkechat.net_ecc",
               .nginx_prefix = "youkechat.net"}};

  // Verify acme.sh directories exist
  for (const auto& domain_config : domains_) {
    if (!FileExists(domain_config.acme_dir)) {
      LOG(WARNING) << "Acme.sh directory does not exist: "
                   << domain_config.acme_dir << " for domain "
                   << domain_config.domain;
    } else {
      LOG(INFO) << "Found acme.sh directory for " << domain_config.domain
                << ": " << domain_config.acme_dir;
    }
  }

  // Ensure nginx ssl directory exists
  if (!EnsureNginxSslDir()) {
    LOG(ERROR) << "Failed to create or access nginx SSL directory: "
               << kNginxSslDir;
    return false;
  }

  initialized_ = true;
  LOG(INFO) << "CertManager initialized successfully for " << domains_.size()
            << " domain(s)";
  return true;
}

CertManager::~CertManager() {
  Stop();
}

void CertManager::Start() {
  if (running_.load()) {
    LOG(WARNING) << "Certificate sync thread is already running";
    return;
  }

  if (!initialized_) {
    LOG(ERROR) << "Cannot start CertManager without initialization";
    return;
  }

  should_stop_ = false;
  running_ = true;

  update_thread_ = std::thread(&CertManager::UpdateLoop, this);
  LOG(INFO) << "Started certificate sync thread with interval "
            << check_interval_seconds_ << " seconds for " << domains_.size()
            << " domain(s)";
}

void CertManager::Stop() {
  if (!running_.load()) {
    return;
  }

  LOG(INFO) << "Stopping certificate sync thread...";

  // Signal the thread to stop
  {
    std::lock_guard<std::mutex> lock(mutex_);
    should_stop_ = true;
  }
  cv_.notify_all();

  // Wait for the thread to finish
  if (update_thread_.joinable()) {
    update_thread_.join();
  }

  running_ = false;
  LOG(INFO) << "Certificate sync thread stopped";
}

bool CertManager::SyncCertificates() {
  if (!initialized_) {
    LOG(ERROR) << "CertManager not initialized";
    return false;
  }

  LOG(INFO) << "Starting certificate sync check for " << domains_.size()
            << " domain(s)";

  bool overall_success = true;
  int synced_count = 0;

  for (const auto& domain_config : domains_) {
    LOG(INFO) << "Checking certificates for domain: " << domain_config.domain;

    if (SyncDomainCertificates(domain_config)) {
      synced_count++;
      LOG(INFO) << "Certificate sync completed for domain: "
                << domain_config.domain;
    } else {
      LOG(ERROR) << "Certificate sync failed for domain: "
                 << domain_config.domain;
      overall_success = false;
    }
  }

  LOG(INFO) << "Certificate sync check completed. Successfully processed "
            << synced_count << "/" << domains_.size() << " domain(s)";

  return overall_success;
}

std::string CertManager::GetCertFileExtension(CertType type) {
  switch (type) {
    case CertType::KEY:
      return ".key";
    case CertType::CA:
      return ".ca.cer";
    case CertType::FULLCHAIN:
      return ".fullchain.cer";
    default:
      return "";
  }
}

std::string CertManager::GetAcmeFilename(const std::string& domain,
                                         CertType type) {
  switch (type) {
    case CertType::KEY:
      return domain + ".key";
    case CertType::CA:
      return "ca.cer";
    case CertType::FULLCHAIN:
      return "fullchain.cer";
    default:
      return "";
  }
}

std::string CertManager::GetNginxFilename(const std::string& domain,
                                          CertType type) {
  return domain + GetCertFileExtension(type);
}

std::string CertManager::CalculateFileHash(const std::string& file_path) {
  std::string hash_result;
  if (util::Util::FileSHA256(file_path, &hash_result)) {
    return hash_result;
  }
  return "";
}

bool CertManager::FileExists(const std::string& file_path) {
  return access(file_path.c_str(), R_OK) == 0;
}

bool CertManager::CopyFile(const std::string& src_path,
                           const std::string& dest_path) {
  try {
    std::filesystem::copy_file(
        src_path, dest_path, std::filesystem::copy_options::overwrite_existing);

    // Set proper permissions for SSL files (readable by owner and group)
    chmod(dest_path.c_str(), 0644);

    LOG(INFO) << "Copied certificate file: " << src_path << " -> " << dest_path;
    return true;
  } catch (const std::exception& e) {
    LOG(ERROR) << "Failed to copy file " << src_path << " to " << dest_path
               << ": " << e.what();
    return false;
  }
}

bool CertManager::SyncDomainCertificates(const DomainConfig& domain_config) {
  // Check if acme.sh directory exists
  if (!FileExists(domain_config.acme_dir)) {
    LOG(WARNING) << "Acme.sh directory not found for domain "
                 << domain_config.domain << ": " << domain_config.acme_dir;
    return true;  // Not a failure, just skip
  }

  bool success = true;

  // Sync each certificate type
  const std::vector<CertType> cert_types = {CertType::KEY, CertType::CA,
                                            CertType::FULLCHAIN};

  for (CertType cert_type : cert_types) {
    if (!SyncCertificateFile(domain_config, cert_type)) {
      success = false;
    }
  }

  return success;
}

bool CertManager::SyncCertificateFile(const DomainConfig& domain_config,
                                      CertType cert_type) {
  // Build file paths
  std::string acme_filename = GetAcmeFilename(domain_config.domain, cert_type);
  std::string nginx_filename =
      GetNginxFilename(domain_config.domain, cert_type);

  std::string src_path = domain_config.acme_dir + "/" + acme_filename;
  std::string dest_path = std::string(kNginxSslDir) + "/" + nginx_filename;

  // Check if source file exists
  if (!FileExists(src_path)) {
    LOG(WARNING) << "Source certificate file not found: " << src_path;
    return true;  // Not a failure, just skip
  }

  // Check if destination file exists
  bool dest_exists = FileExists(dest_path);
  bool need_copy = false;

  if (!dest_exists) {
    LOG(INFO) << "Destination certificate file missing, will copy: "
              << dest_path;
    need_copy = true;
  } else {
    // Compare hashes to see if files are different
    std::string src_hash = CalculateFileHash(src_path);
    std::string dest_hash = CalculateFileHash(dest_path);

    if (src_hash.empty() || dest_hash.empty()) {
      LOG(ERROR) << "Failed to calculate hash for comparison: " << src_path
                 << " or " << dest_path;
      return false;
    }

    if (src_hash != dest_hash) {
      LOG(INFO) << "Certificate file hash mismatch, will update: " << dest_path
                << " (src: " << src_hash.substr(0, 16) << "..."
                << ", dest: " << dest_hash.substr(0, 16) << "...)";
      need_copy = true;
    } else {
      LOG(INFO) << "Certificate file up to date: " << dest_path;
    }
  }

  // Copy file if needed
  if (need_copy) {
    return CopyFile(src_path, dest_path);
  }

  return true;
}

bool CertManager::EnsureNginxSslDir() {
  try {
    std::filesystem::create_directories(kNginxSslDir);

    // Verify directory is accessible
    if (!FileExists(kNginxSslDir)) {
      LOG(ERROR) << "Failed to access nginx SSL directory after creation: "
                 << kNginxSslDir;
      return false;
    }

    LOG(INFO) << "Nginx SSL directory ready: " << kNginxSslDir;
    return true;
  } catch (const std::exception& e) {
    LOG(ERROR) << "Failed to create nginx SSL directory " << kNginxSslDir
               << ": " << e.what();
    return false;
  }
}

void CertManager::UpdateLoop() {
  LOG(INFO) << "Certificate sync loop started";

  // Perform initial sync
  LOG(INFO) << "Performing initial certificate sync...";
  SyncCertificates();

  // Main update loop
  while (true) {
    // Wait for the configured interval or until stop signal
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (cv_.wait_for(lock, std::chrono::seconds(check_interval_seconds_),
                       [this] { return should_stop_.load(); })) {
        // should_stop_ became true, exit loop
        break;
      }
    }

    // Sync certificates
    try {
      bool success = SyncCertificates();
      if (success) {
        LOG(INFO) << "Certificate sync successful, next check in "
                  << check_interval_seconds_ << " seconds";
      } else {
        LOG(WARNING) << "Certificate sync had some failures, next check in "
                     << check_interval_seconds_ << " seconds";
      }
    } catch (const std::exception& e) {
      LOG(ERROR) << "Exception in certificate sync: " << e.what();
    }
  }

  LOG(INFO) << "Certificate sync loop stopped";
}

}  // namespace impl
}  // namespace tbox
