/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/impl/ddns_manager.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "aws/core/Aws.h"
#include "aws/core/auth/AWSCredentials.h"
#include "aws/core/client/ClientConfiguration.h"
#include "aws/route53/Route53Client.h"
#include "aws/route53/model/Change.h"
#include "aws/route53/model/ChangeBatch.h"
#include "aws/route53/model/ChangeResourceRecordSetsRequest.h"
#include "aws/route53/model/ListHostedZonesRequest.h"
#include "aws/route53/model/ListResourceRecordSetsRequest.h"
#include "aws/route53/model/ResourceRecord.h"
#include "aws/route53/model/ResourceRecordSet.h"
#include "glog/logging.h"
#include "src/impl/config_manager.h"
#include "src/util/util.h"

namespace tbox {
namespace impl {

static folly::Singleton<DDNSManager> ddns_manager;

std::shared_ptr<DDNSManager> DDNSManager::Instance() {
  return ddns_manager.try_get();
}

DDNSManager::DDNSManager() : running_(false), should_stop_(false) {}

bool DDNSManager::Init() {
  std::lock_guard<std::mutex> lock(init_mutex_);

  if (initialized_) {
    LOG(WARNING) << "DDNSManager already initialized";
    return true;
  }

  // Initialize AWS SDK
  Aws::SDKOptions aws_options;
  Aws::InitAPI(aws_options);
  LOG(INFO) << "AWS SDK initialized for DDNSManager";

  // Get monitor domains from ConfigManager
  auto config_manager = util::ConfigManager::Instance();
  monitor_domains_ = config_manager->MonitorDomains();

  if (monitor_domains_.empty()) {
    LOG(WARNING) << "No monitor domains configured";
    initialized_ = true;
    return true;
  }

  // Create Route53 client
  Aws::Client::ClientConfiguration client_config;
  client_config.region = kAwsRegion;

  route53_client_ =
      std::make_unique<Aws::Route53::Route53Client>(client_config);

  // Get hosted zone IDs for all monitor domains
  for (const auto& domain : monitor_domains_) {
    std::string zone_id = GetHostedZoneId(domain);
    if (zone_id.empty()) {
      LOG(ERROR) << "Failed to get hosted zone ID for domain: " << domain;
      return false;
    }
    domain_to_zone_id_[domain] = zone_id;
    LOG(INFO) << "Found hosted zone ID for " << domain << ": " << zone_id;
  }

  initialized_ = true;
  return true;
}

DDNSManager::~DDNSManager() {
  Stop();

  // Shutdown AWS SDK if it was initialized
  if (initialized_) {
    Aws::SDKOptions aws_options;
    Aws::ShutdownAPI(aws_options);
    LOG(INFO) << "AWS SDK shutdown for DDNSManager";
  }
}

void DDNSManager::Start() {
  if (running_.load()) {
    LOG(WARNING) << "DDNS update thread is already running";
    return;
  }

  if (domain_to_zone_id_.empty()) {
    LOG(ERROR) << "Cannot start DDNS manager without valid hosted zone IDs";
    return;
  }

  should_stop_ = false;
  running_ = true;

  update_thread_ = std::thread(&DDNSManager::UpdateLoop, this);
  LOG(INFO) << "Started DDNS update thread with interval "
            << kCheckIntervalSeconds << " seconds for "
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

// Check if a specific IPv6 is in the list
bool DDNSManager::IsIPv6InList(const std::string& ipv6,
                               const std::vector<std::string>& list) {
  return std::find(list.begin(), list.end(), ipv6) != list.end();
}

// Determine if Route53 record needs updating
bool DDNSManager::ShouldUpdateRecord(
    const std::vector<std::string>& route53_records,
    const std::vector<std::string>& public_ips) {
  // No records exist - need to create
  if (route53_records.empty()) {
    LOG(INFO) << "No Route53 AAAA records found. Need to create.";
    return true;
  }

  // Check if any resolved address matches any current public IP
  for (const auto& resolved_ip : route53_records) {
    if (IsIPv6InList(resolved_ip, public_ips)) {
      LOG(INFO) << "Route53 record " << resolved_ip
                << " matches one of the current public IPs. No update needed.";
      return false;
    }
  }

  // No match found - update needed
  LOG(INFO) << "None of the Route53 records match current public IPs. "
            << "Update needed.";
  return true;
}

// Get Route53 Hosted Zone ID for the domain
std::string DDNSManager::GetHostedZoneId(const std::string& domain) {
  if (!route53_client_) {
    LOG(ERROR) << "Route53 client not initialized";
    return "";
  }

  Aws::Route53::Model::ListHostedZonesRequest request;
  auto outcome = route53_client_->ListHostedZones(request);

  if (!outcome.IsSuccess()) {
    LOG(ERROR) << "Failed to list hosted zones: "
               << outcome.GetError().GetMessage();
    return "";
  }

  std::string search_domain = domain;
  if (search_domain.back() != '.') {
    search_domain += '.';
  }

  for (const auto& zone : outcome.GetResult().GetHostedZones()) {
    if (zone.GetName() == search_domain) {
      // Extract zone ID (format is "/hostedzone/XXXXX")
      std::string zone_id = zone.GetId();
      size_t pos = zone_id.find_last_of('/');
      if (pos != std::string::npos) {
        return zone_id.substr(pos + 1);
      }
      return zone_id;
    }
  }

  LOG(ERROR) << "Hosted zone not found for domain: " << domain;
  return "";
}

// Get current AAAA records for the domain from Route53
std::vector<std::string> DDNSManager::GetCurrentRoute53Records(
    const std::string& hosted_zone_id, const std::string& domain) {
  std::vector<std::string> records;

  if (!route53_client_) {
    LOG(ERROR) << "Route53 client not initialized";
    return records;
  }

  Aws::Route53::Model::ListResourceRecordSetsRequest request;
  request.SetHostedZoneId(hosted_zone_id);

  auto outcome = route53_client_->ListResourceRecordSets(request);
  if (!outcome.IsSuccess()) {
    LOG(ERROR) << "Failed to list resource record sets: "
               << outcome.GetError().GetMessage();
    return records;
  }

  std::string search_name = domain;
  if (search_name.back() != '.') {
    search_name += '.';
  }

  for (const auto& record_set : outcome.GetResult().GetResourceRecordSets()) {
    if (record_set.GetName() == search_name &&
        record_set.GetType() == Aws::Route53::Model::RRType::AAAA) {
      for (const auto& record : record_set.GetResourceRecords()) {
        records.push_back(record.GetValue());
      }
    }
  }

  return records;
}

// Update Route53 record with new IPv6 address
bool DDNSManager::UpdateRoute53Record(const std::string& hosted_zone_id,
                                      const std::string& domain,
                                      const std::string& ipv6) {
  if (!route53_client_) {
    LOG(ERROR) << "Route53 client not initialized";
    return false;
  }

  Aws::Route53::Model::ChangeResourceRecordSetsRequest request;
  request.SetHostedZoneId(hosted_zone_id);

  // Create resource record
  Aws::Route53::Model::ResourceRecord record;
  record.SetValue(ipv6);

  // Create resource record set
  Aws::Route53::Model::ResourceRecordSet record_set;
  std::string record_name = domain;
  if (record_name.back() != '.') {
    record_name += '.';
  }
  record_set.SetName(record_name);
  record_set.SetType(Aws::Route53::Model::RRType::AAAA);
  record_set.SetTTL(kDnsTtl);
  record_set.AddResourceRecords(record);

  // Create change
  Aws::Route53::Model::Change change;
  change.SetAction(Aws::Route53::Model::ChangeAction::UPSERT);
  change.SetResourceRecordSet(record_set);

  // Create change batch
  Aws::Route53::Model::ChangeBatch change_batch;
  change_batch.AddChanges(change);
  change_batch.SetComment("Updated by DDNS manager");

  request.SetChangeBatch(change_batch);

  auto outcome = route53_client_->ChangeResourceRecordSets(request);
  if (!outcome.IsSuccess()) {
    LOG(ERROR) << "Failed to update Route53 record: "
               << outcome.GetError().GetMessage();
    return false;
  }

  LOG(INFO) << "Successfully updated Route53 record: " << domain << " -> "
            << ipv6;
  return true;
}

bool DDNSManager::UpdateDNS() {
  if (domain_to_zone_id_.empty()) {
    LOG(ERROR) << "No hosted zone IDs available";
    return false;
  }

  LOG(INFO) << "=== Checking IPv6 and DNS ===";

  // Get current public IPv6 addresses
  auto public_ipv6s = util::Util::GetPublicIPv6Addresses();

  if (public_ipv6s.empty()) {
    LOG(WARNING) << "No public IPv6 addresses found";
    return false;
  }

  LOG(INFO) << "Current public IPv6 addresses:";
  for (const auto& ipv6 : public_ipv6s) {
    LOG(INFO) << "  - " << ipv6;
  }

  // Update all monitored domains
  bool all_success = true;
  for (const auto& [domain, zone_id] : domain_to_zone_id_) {
    LOG(INFO) << "Checking domain: " << domain;

    // Get actual Route53 records (source of truth)
    auto route53_ipv6s = GetCurrentRoute53Records(zone_id, domain);
    LOG(INFO) << "Route53 AAAA records for " << domain << ":";
    for (const auto& ipv6 : route53_ipv6s) {
      LOG(INFO) << "  - " << ipv6;
    }

    // Check if update is needed
    bool needs_update = ShouldUpdateRecord(route53_ipv6s, public_ipv6s);

    if (needs_update) {
      // Use the first address as the primary one for update
      std::string primary_ipv6 = public_ipv6s[0];
      LOG(INFO) << "Updating Route53 for " << domain
                << " with primary IPv6: " << primary_ipv6;
      if (!UpdateRoute53Record(zone_id, domain, primary_ipv6)) {
        all_success = false;
      }
    } else {
      LOG(INFO) << "Route53 record for " << domain << " is up to date.";
    }
  }

  return all_success;
}

int DDNSManager::CalculateBackoff() {
  if (consecutive_failures_ == 0) {
    return kCheckIntervalSeconds;
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
                  << kCheckIntervalSeconds << " seconds";
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
