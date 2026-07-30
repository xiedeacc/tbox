/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/impl/ddns_manager.h"

#if defined(_WIN32)
#include <cstdio>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "src/common/logging.h"
#include "src/impl/config_manager.h"
#include "src/impl/dns/dns_provider.h"
#include "src/util/util.h"

namespace tbox {
namespace impl {

std::shared_ptr<DDNSManager> DDNSManager::Instance() {
  static std::shared_ptr<DDNSManager> instance(new DDNSManager());
  return instance;
}

DDNSManager::DDNSManager() : running_(false), should_stop_(false) {}

bool DDNSManager::Init() {
  std::lock_guard<std::mutex> lock(init_mutex_);

  if (initialized_) {
    LOG(WARNING) << "DDNSManager already initialized";
    return true;
  }

  auto config_manager = util::ConfigManager::Instance();
  monitor_domains_ = config_manager->MonitorDomains();
  check_interval_seconds_ = config_manager->CheckIntervalSeconds();

  if (monitor_domains_.empty()) {
    LOG(WARNING) << "No monitor domains configured";
    initialized_ = true;
    return true;
  }

  provider_ = dns::CreateDnsProvider();
  if (provider_ == nullptr) {
    LOG(ERROR) << "Failed to create DNS provider";
    return false;
  }
  if (!provider_->Init()) {
    LOG(ERROR) << "Failed to initialize DNS provider: " << provider_->Name();
    return false;
  }
  LOG(INFO) << "DDNS using DNS provider: " << provider_->Name();

  for (const auto& domain : monitor_domains_) {
    const std::string zone_id = provider_->GetZoneId(domain);
    if (zone_id.empty()) {
      LOG(ERROR) << "Failed to get zone ID for domain: " << domain;
      return false;
    }
    domain_to_zone_id_[domain] = zone_id;
    LOG(INFO) << "Mapped domain " << domain << " to zone: " << zone_id;
  }

  initialized_ = true;
  return true;
}

DDNSManager::~DDNSManager() { Stop(); }

void DDNSManager::Start() {
  if (running_.load()) {
    LOG(WARNING) << "DDNS update thread is already running";
    return;
  }

  if (domain_to_zone_id_.empty()) {
    LOG(ERROR) << "Cannot start DDNS manager without valid zone IDs";
    return;
  }

  should_stop_ = false;
  running_ = true;

  update_thread_ = std::thread(&DDNSManager::UpdateLoop, this);
  LOG(INFO) << "Started DDNS update thread with interval "
            << check_interval_seconds_ << " seconds for "
            << domain_to_zone_id_.size() << " domain(s)";
}

void DDNSManager::Stop() {
  if (!running_.load()) {
    return;
  }

  LOG(INFO) << "Stopping DDNS update thread...";

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
  LOG(INFO) << "DDNS update thread stopped";
}

// Check if a specific IP is in the list
bool DDNSManager::IsIPInList(const std::string& ip,
                             const std::vector<std::string>& list) {
  return std::find(list.begin(), list.end(), ip) != list.end();
}

// Check if IP address is private/loopback
bool DDNSManager::IsPrivateIP(const std::string& ip) {
  try {
    folly::IPAddress addr(ip);
    return addr.isLoopback() || addr.isPrivate() || addr.isLinkLocal();
  } catch (const std::exception& e) {
    LOG(WARNING) << "Failed to parse IP address: " << ip << " - " << e.what();
    return true;  // Treat parse errors as private for safety
  }
}

// Get public IPv4 address from external service
std::string DDNSManager::GetPublicIPv4() {
  try {
#if defined(_WIN32)
    FILE* pipe = _popen("curl -4 -s --max-time 5 https://api.ipify.org", "r");
#else
    FILE* pipe = popen("curl -4 -s --max-time 5 https://api.ipify.org", "r");
#endif
    if (!pipe) {
      LOG(WARNING) << "Failed to execute curl for public IPv4";
      return "";
    }

    char buffer[128];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      result += buffer;
    }
#if defined(_WIN32)
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    // Trim whitespace and newlines
    result.erase(0, result.find_first_not_of(" \t\n\r"));
    result.erase(result.find_last_not_of(" \t\n\r") + 1);

    // Validate it's an IPv4 address
    if (!result.empty()) {
      folly::IPAddressV4 addr(result);  // Will throw if invalid
      return result;
    }
  } catch (const std::exception& e) {
    LOG(WARNING) << "Failed to get public IPv4: " << e.what();
  }
  return "";
}

bool DDNSManager::ReconcileRecord(const std::string& zone_id,
                                  const std::string& domain,
                                  dns::RecordType type,
                                  const std::string& desired,
                                  std::vector<std::string>* log_buffer,
                                  bool* updated) {
  const char* type_name = dns::RecordTypeToString(type);

  std::vector<dns::Record> records;
  if (!provider_->ListRecords(zone_id, domain, type, &records)) {
    log_buffer->push_back(std::string("Failed to list ") + type_name +
                          " records for " + domain);
    return false;
  }

  if (records.empty()) {
    log_buffer->push_back(std::string("No ") + type_name + " records for " +
                          domain + " - skipping");
    return true;
  }

  bool all_success = true;

  // Drop any record that leaked a private address.
  for (const auto& record : records) {
    if (!IsPrivateIP(record.value)) {
      continue;
    }
    *updated = true;
    log_buffer->push_back(std::string("Found private ") + type_name + ": " +
                          record.value + " - deleting");
    if (!provider_->DeleteRecord(zone_id, domain, type, record.value)) {
      all_success = false;
    }
  }

  if (!provider_->ListRecords(zone_id, domain, type, &records)) {
    log_buffer->push_back(std::string("Failed to re-list ") + type_name +
                          " records for " + domain);
    return false;
  }

  std::vector<std::string> values;
  values.reserve(records.size());
  for (const auto& record : records) {
    values.push_back(record.value);
  }

  if (desired.empty()) {
    return all_success;
  }

  if (IsIPInList(desired, values)) {
    log_buffer->push_back(std::string(type_name) + " record for " + domain +
                          " is up to date - no update needed");
    return all_success;
  }

  *updated = true;
  if (provider_->UpsertRecord(zone_id, domain, type, desired, kDnsTtl)) {
    log_buffer->push_back(std::string(type_name) + " record for " + domain +
                          " updated successfully -> " + desired);
  } else {
    log_buffer->push_back(std::string("Failed to update ") + type_name +
                          " record for " + domain);
    all_success = false;
  }
  return all_success;
}

bool DDNSManager::UpdateDNS() {
  if (domain_to_zone_id_.empty()) {
    LOG(ERROR) << "No zone IDs available";
    return false;
  }

  // Buffer all log messages for atomic output
  std::vector<std::string> log_buffer;
  log_buffer.push_back("=== Checking IPv4 and IPv6 DNS Records ===");

  // Get current public IPv4 address from external service
  std::string public_ipv4 = GetPublicIPv4();
  if (!public_ipv4.empty()) {
    log_buffer.push_back("Current public IPv4 address: " + public_ipv4);
  } else {
    log_buffer.push_back("Failed to get public IPv4 address");
  }

  // Get current public IPv6 addresses, sorted by longest prefix.
  auto public_ipv6s = util::Util::GetPublicIPv6Addresses();
  std::string primary_ipv6;
  for (const auto& ipv6 : public_ipv6s) {
    if (!IsPrivateIP(ipv6)) {
      primary_ipv6 = ipv6;
      break;
    }
  }
  if (!public_ipv6s.empty()) {
    std::string ipv6_list;
    for (size_t i = 0; i < public_ipv6s.size(); ++i) {
      if (i > 0) {
        ipv6_list += ", ";
      }
      ipv6_list += public_ipv6s[i];
    }
    log_buffer.push_back("Current public IPv6 addresses: " + ipv6_list);
  } else {
    log_buffer.push_back("No public IPv6 addresses found");
  }

  bool all_success = true;
  bool any_updates_needed = false;
  for (const auto& [domain, zone_id] : domain_to_zone_id_) {
    log_buffer.push_back("Checking domain: " + domain);

    if (!ReconcileRecord(zone_id, domain, dns::RecordType::kA, public_ipv4,
                         &log_buffer, &any_updates_needed)) {
      all_success = false;
    }
    if (!ReconcileRecord(zone_id, domain, dns::RecordType::kAAAA, primary_ipv6,
                         &log_buffer, &any_updates_needed)) {
      all_success = false;
    }
  }

  // Only output logs if updates were needed or if there were errors
  if (any_updates_needed || !all_success) {
    for (const auto& msg : log_buffer) {
      LOG(INFO) << msg;
    }
  } else {
    LOG(INFO) << "DNS records are up to date - no updates needed";
  }

  return all_success;
}

int DDNSManager::CalculateBackoff() {
  if (consecutive_failures_ == 0) {
    return check_interval_seconds_;
  }

  // Exponential backoff: min_backoff * (2 ^ failures)
  int backoff = kMinBackoffSeconds *
                (1 << std::min(consecutive_failures_, 10));  // Cap at 2^10

  // Clamp to max_backoff
  return std::min(backoff, kMaxBackoffSeconds);
}

void DDNSManager::UpdateLoop() {
  LOG(INFO) << "DDNS update loop started";

  // Main update loop
  while (true) {
    // Calculate wait interval (normal or backoff)
    current_backoff_seconds_ = CalculateBackoff();

    // Wait for the configured interval or until stop signal
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (cv_.wait_for(lock, std::chrono::seconds(current_backoff_seconds_),
                       [this] { return should_stop_.load(); })) {
        // should_stop_ became true, exit loop
        break;
      }
    }

    // Update DNS if needed
    try {
      bool success = UpdateDNS();
      if (success) {
        // Reset failure counter on success
        consecutive_failures_ = 0;
        LOG(INFO) << "DDNS update successful, next check in "
                  << check_interval_seconds_ << " seconds";
      } else {
        // Increment failure counter
        consecutive_failures_++;
        int next_backoff = CalculateBackoff();
        LOG(WARNING) << "DDNS update failed (attempt " << consecutive_failures_
                     << "), will retry in " << next_backoff
                     << " seconds (exponential backoff)";
      }
    } catch (const std::exception& e) {
      LOG(ERROR) << "Exception in DDNS update loop: " << e.what();
      consecutive_failures_++;
      int next_backoff = CalculateBackoff();
      LOG(WARNING) << "Will retry in " << next_backoff
                   << " seconds (exponential backoff)";
    }
  }

  LOG(INFO) << "DDNS update loop ended";
}

}  // namespace impl
}  // namespace tbox
