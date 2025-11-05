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

  // Get monitor domains and check interval from ConfigManager
  auto config_manager = util::ConfigManager::Instance();
  monitor_domains_ = config_manager->MonitorDomains();
  check_interval_seconds_ = config_manager->CheckIntervalSeconds();

  if (monitor_domains_.empty()) {
    LOG(WARNING) << "No monitor domains configured";
    initialized_ = true;
    return true;
  }

  // Create Route53 client
  Aws::Client::ClientConfiguration client_config;

  // Use region from config if available, otherwise use default
  std::string aws_region = config_manager->AwsRegion();
  if (!aws_region.empty()) {
    client_config.region = aws_region;
    LOG(INFO) << "Using AWS region from config: " << aws_region;
  } else {
    client_config.region = kAwsRegion;
    LOG(INFO) << "Using default AWS region: " << kAwsRegion;
  }

  // Check if AWS credentials are provided in config
  std::string access_key_id = config_manager->AwsAccessKeyId();
  std::string secret_access_key = config_manager->AwsSecretAccessKey();

  if (!access_key_id.empty() && !secret_access_key.empty()) {
    // Use credentials from config
    Aws::Auth::AWSCredentials credentials(access_key_id.c_str(),
                                          secret_access_key.c_str());
    route53_client_ = std::make_unique<Aws::Route53::Route53Client>(
        credentials, client_config);
    LOG(INFO) << "Using AWS credentials from config file";
  } else {
    // Use default credential chain (environment variables, ~/.aws/credentials,
    // etc.)
    route53_client_ =
        std::make_unique<Aws::Route53::Route53Client>(client_config);
    LOG(INFO) << "Using AWS default credential chain";
  }

  // Get hosted zone ID from config or query Route53
  std::string configured_zone_id = config_manager->Route53HostedZoneId();

  if (!configured_zone_id.empty()) {
    // Use configured hosted zone ID for all monitor domains
    LOG(INFO) << "Using configured Route53 hosted zone ID: "
              << configured_zone_id;
    for (const auto& domain : monitor_domains_) {
      domain_to_zone_id_[domain] = configured_zone_id;
      LOG(INFO) << "Mapped domain " << domain
                << " to zone: " << configured_zone_id;
    }
  } else {
    // Query Route53 to get hosted zone IDs for each domain
    LOG(INFO) << "No configured zone ID, querying Route53 for each domain";
    for (const auto& domain : monitor_domains_) {
      std::string zone_id = GetHostedZoneId(domain);
      if (zone_id.empty()) {
        LOG(ERROR) << "Failed to get hosted zone ID for domain: " << domain;
        return false;
      }
      domain_to_zone_id_[domain] = zone_id;
      LOG(INFO) << "Found hosted zone ID for " << domain << ": " << zone_id;
    }
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
    FILE* pipe = popen("curl -4 -s --max-time 5 https://api.ipify.org", "r");
    if (!pipe) {
      LOG(WARNING) << "Failed to execute curl for public IPv4";
      return "";
    }

    char buffer[128];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      result += buffer;
    }
    pclose(pipe);

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
std::vector<std::string> DDNSManager::GetCurrentRoute53ARecords(
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
        record_set.GetType() == Aws::Route53::Model::RRType::A) {
      for (const auto& record : record_set.GetResourceRecords()) {
        records.push_back(record.GetValue());
      }
    }
  }

  return records;
}

std::vector<std::string> DDNSManager::GetCurrentRoute53AAAARecords(
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

// Update Route53 A record with new IPv4 address
bool DDNSManager::UpdateRoute53ARecord(const std::string& hosted_zone_id,
                                       const std::string& domain,
                                       const std::string& ipv4) {
  if (!route53_client_) {
    LOG(ERROR) << "Route53 client not initialized";
    return false;
  }

  Aws::Route53::Model::ChangeResourceRecordSetsRequest request;
  request.SetHostedZoneId(hosted_zone_id);

  // Create resource record
  Aws::Route53::Model::ResourceRecord record;
  record.SetValue(ipv4);

  // Create resource record set
  Aws::Route53::Model::ResourceRecordSet record_set;
  std::string record_name = domain;
  if (record_name.back() != '.') {
    record_name += '.';
  }
  record_set.SetName(record_name);
  record_set.SetType(Aws::Route53::Model::RRType::A);
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
    LOG(ERROR) << "Failed to update Route53 A record: "
               << outcome.GetError().GetMessage();
    return false;
  }

  LOG(INFO) << "Successfully updated Route53 A record: " << domain << " -> "
            << ipv4;
  return true;
}

// Update Route53 AAAA record with new IPv6 address
bool DDNSManager::UpdateRoute53AAAARecord(const std::string& hosted_zone_id,
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
    LOG(ERROR) << "Failed to update Route53 AAAA record: "
               << outcome.GetError().GetMessage();
    return false;
  }

  LOG(INFO) << "Successfully updated Route53 AAAA record: " << domain << " -> "
            << ipv6;
  return true;
}

// Delete Route53 record
bool DDNSManager::DeleteRoute53Record(const std::string& hosted_zone_id,
                                      const std::string& domain,
                                      const std::string& record_type,
                                      const std::string& ip) {
  if (!route53_client_) {
    LOG(ERROR) << "Route53 client not initialized";
    return false;
  }

  Aws::Route53::Model::ChangeResourceRecordSetsRequest request;
  request.SetHostedZoneId(hosted_zone_id);

  // Create resource record
  Aws::Route53::Model::ResourceRecord record;
  record.SetValue(ip);

  // Create resource record set
  Aws::Route53::Model::ResourceRecordSet record_set;
  std::string record_name = domain;
  if (record_name.back() != '.') {
    record_name += '.';
  }
  record_set.SetName(record_name);
  record_set.SetType(record_type == "A" ? Aws::Route53::Model::RRType::A
                                        : Aws::Route53::Model::RRType::AAAA);
  record_set.SetTTL(kDnsTtl);
  record_set.AddResourceRecords(record);

  // Create change
  Aws::Route53::Model::Change change;
  change.SetAction(Aws::Route53::Model::ChangeAction::DELETE_);
  change.SetResourceRecordSet(record_set);

  // Create change batch
  Aws::Route53::Model::ChangeBatch change_batch;
  change_batch.AddChanges(change);
  change_batch.SetComment("Deleted private IP by DDNS manager");

  request.SetChangeBatch(change_batch);

  auto outcome = route53_client_->ChangeResourceRecordSets(request);
  if (!outcome.IsSuccess()) {
    LOG(ERROR) << "Failed to delete Route53 " << record_type
               << " record: " << outcome.GetError().GetMessage();
    return false;
  }

  LOG(WARNING) << "Deleted private " << record_type
               << " record from Route53: " << domain << " -> " << ip;
  return true;
}

bool DDNSManager::UpdateDNS() {
  if (domain_to_zone_id_.empty()) {
    LOG(ERROR) << "No hosted zone IDs available";
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

  // Get current public IPv6 addresses
  auto public_ipv6s = util::Util::GetPublicIPv6Addresses();
  if (!public_ipv6s.empty()) {
    std::string ipv6_list;
    for (size_t i = 0; i < public_ipv6s.size(); ++i) {
      if (i > 0)
        ipv6_list += ", ";
      ipv6_list += public_ipv6s[i];
    }
    log_buffer.push_back("Current public IPv6 addresses: " + ipv6_list);
  } else {
    log_buffer.push_back("No public IPv6 addresses found");
  }

  // Update all monitored domains
  bool all_success = true;
  for (const auto& [domain, zone_id] : domain_to_zone_id_) {
    log_buffer.push_back("Checking domain: " + domain);

    // ===  Handle IPv4 (A records) ===
    auto route53_ipv4s = GetCurrentRoute53ARecords(zone_id, domain);
    bool has_ipv4_records = !route53_ipv4s.empty();

    if (has_ipv4_records) {
      // Check for and delete private IPv4 addresses
      for (const auto& ipv4 : route53_ipv4s) {
        if (IsPrivateIP(ipv4)) {
          log_buffer.push_back("Found private IPv4 in Route53: " + ipv4 +
                               " - deleting");
          if (!DeleteRoute53Record(zone_id, domain, "A", ipv4)) {
            all_success = false;
          }
        }
      }

      // Get updated list after deletions
      route53_ipv4s = GetCurrentRoute53ARecords(zone_id, domain);

      std::string ipv4_list;
      for (size_t i = 0; i < route53_ipv4s.size(); ++i) {
        if (i > 0)
          ipv4_list += ", ";
        ipv4_list += route53_ipv4s[i];
      }
      if (!ipv4_list.empty()) {
        log_buffer.push_back("Route53 A records for " + domain + ": " +
                             ipv4_list);
      } else {
        log_buffer.push_back("Route53 A records deleted (were all private)");
      }

      // Update IPv4 if we have a public address
      if (!public_ipv4.empty()) {
        bool ipv4_needs_update = !IsIPInList(public_ipv4, route53_ipv4s);
        if (ipv4_needs_update) {
          if (UpdateRoute53ARecord(zone_id, domain, public_ipv4)) {
            log_buffer.push_back("Route53 A record for " + domain +
                                 " updated successfully -> " + public_ipv4);
          } else {
            log_buffer.push_back("Failed to update Route53 A record for " +
                                 domain);
            all_success = false;
          }
        } else {
          log_buffer.push_back("Route53 A record for " + domain +
                               " is up to date - no update needed");
        }
      }
    } else {
      log_buffer.push_back("No A records in Route53 for " + domain +
                           " - skipping IPv4 handling");
    }

    // === Handle IPv6 (AAAA records) ===
    auto route53_ipv6s = GetCurrentRoute53AAAARecords(zone_id, domain);
    bool has_ipv6_records = !route53_ipv6s.empty();

    if (has_ipv6_records) {
      // Check for and delete private IPv6 addresses
      for (const auto& ipv6 : route53_ipv6s) {
        if (IsPrivateIP(ipv6)) {
          log_buffer.push_back("Found private IPv6 in Route53: " + ipv6 +
                               " - deleting");
          if (!DeleteRoute53Record(zone_id, domain, "AAAA", ipv6)) {
            all_success = false;
          }
        }
      }

      // Get updated list after deletions
      route53_ipv6s = GetCurrentRoute53AAAARecords(zone_id, domain);

      std::string ipv6_list;
      for (size_t i = 0; i < route53_ipv6s.size(); ++i) {
        if (i > 0)
          ipv6_list += ", ";
        ipv6_list += route53_ipv6s[i];
      }
      if (!ipv6_list.empty()) {
        log_buffer.push_back("Route53 AAAA records for " + domain + ": " +
                             ipv6_list);
      } else {
        log_buffer.push_back("Route53 AAAA records deleted (were all private)");
      }

      // Update IPv6 if we have public addresses
      if (!public_ipv6s.empty()) {
        // Check if all local IPv6 addresses are private
        bool all_private = true;
        for (const auto& ipv6 : public_ipv6s) {
          if (!IsPrivateIP(ipv6)) {
            all_private = false;
            break;
          }
        }

        if (all_private) {
          log_buffer.push_back(
              "All local IPv6 addresses are private - skipping update");
        } else {
          // Use the primary (first) IPv6 address - sorted by longest prefix
          std::string primary_ipv6 = public_ipv6s[0];
          bool ipv6_needs_update = true;

          // Check if Route53 already has the primary IPv6 address
          for (const auto& route53_ipv6 : route53_ipv6s) {
            if (route53_ipv6 == primary_ipv6) {
              ipv6_needs_update = false;
              break;
            }
          }

          if (ipv6_needs_update) {
            if (UpdateRoute53AAAARecord(zone_id, domain, primary_ipv6)) {
              log_buffer.push_back("Route53 AAAA record for " + domain +
                                   " updated to primary IPv6 -> " +
                                   primary_ipv6);
            } else {
              log_buffer.push_back("Failed to update Route53 AAAA record for " +
                                   domain);
              all_success = false;
            }
          } else {
            log_buffer.push_back(
                "Route53 AAAA record for " + domain +
                " is up to date with primary IPv6: " + primary_ipv6);
          }
        }
      }
    } else {
      log_buffer.push_back("No AAAA records in Route53 for " + domain +
                           " - skipping IPv6 handling");
    }
  }

  // Output all buffered logs atomically
  for (const auto& msg : log_buffer) {
    LOG(INFO) << msg;
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
