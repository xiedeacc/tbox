/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/impl/ddns_manager.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "folly/IPAddress.h"
#include "src/common/logging.h"
#include "src/impl/dns/dns_provider.h"

namespace tbox {
namespace impl {

std::shared_ptr<DDNSManager> DDNSManager::Instance() {
  static std::shared_ptr<DDNSManager> instance(new DDNSManager());
  return instance;
}

bool DDNSManager::Init() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (initialized_) {
    return true;
  }

  provider_ = dns::CreateDnsProvider();
  if (!provider_ || !provider_->Init()) {
    LOG(ERROR) << "Failed to initialize server-side DNS provider";
    return false;
  }
  initialized_ = true;
  LOG(INFO) << "Server-side DDNS using DNS provider: " << provider_->Name();
  return true;
}

void DDNSManager::SetProviderForTesting(
    std::unique_ptr<dns::DnsProvider> provider) {
  std::lock_guard<std::mutex> lock(mutex_);
  provider_ = std::move(provider);
  initialized_ = provider_ != nullptr;
  domain_to_zone_id_.clear();
  last_record_values_.clear();
}

std::string DDNSManager::NormalizeDomain(const std::string& domain) {
  std::string normalized = domain;
  while (!normalized.empty() && normalized.back() == '.') {
    normalized.pop_back();
  }
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  return normalized;
}

bool DDNSManager::IsValidDomain(const std::string& domain) {
  if (domain.empty() || domain.size() > 253 ||
      domain.find('.') == std::string::npos) {
    return false;
  }

  size_t label_start = 0;
  while (label_start < domain.size()) {
    const size_t dot = domain.find('.', label_start);
    const size_t label_end =
        dot == std::string::npos ? domain.size() : dot;
    const size_t length = label_end - label_start;
    if (length == 0 || length > 63 || domain[label_start] == '-' ||
        domain[label_end - 1] == '-') {
      return false;
    }
    for (size_t i = label_start; i < label_end; ++i) {
      const unsigned char value =
          static_cast<unsigned char>(domain[i]);
      if (!std::isalnum(value) && value != '-') {
        return false;
      }
    }
    if (dot == std::string::npos) {
      break;
    }
    label_start = dot + 1;
  }
  return true;
}

std::set<dns::RecordType> DDNSManager::ParseRecordTypes(
    const std::vector<std::string>& record_types) {
  std::set<dns::RecordType> result;
  for (std::string type : record_types) {
    std::transform(type.begin(), type.end(), type.begin(),
                   [](unsigned char value) {
                     return static_cast<char>(std::toupper(value));
                   });
    if (type == "A") {
      result.insert(dns::RecordType::kA);
    } else if (type == "AAAA") {
      result.insert(dns::RecordType::kAAAA);
    }
  }
  if (result.empty()) {
    result.insert(dns::RecordType::kA);
    result.insert(dns::RecordType::kAAAA);
  }
  return result;
}

std::map<dns::RecordType, std::string>
DDNSManager::SelectPublicAddresses(
    const std::vector<std::string>& addresses) {
  std::map<dns::RecordType, std::string> selected;
  for (const auto& address : addresses) {
    try {
      const folly::IPAddress ip(address);
      if (ip.isLoopback() || ip.isPrivate() || ip.isLinkLocal() ||
          ip.isMulticast() || address == "0.0.0.0" || address == "::") {
        continue;
      }
      const dns::RecordType type =
          ip.isV4() ? dns::RecordType::kA : dns::RecordType::kAAAA;
      selected.try_emplace(type, ip.str());
    } catch (const std::exception&) {
      LOG(WARNING) << "Ignoring invalid reported IP address";
    }
  }
  return selected;
}

bool DDNSManager::ReconcileRecord(const std::string& zone_id,
                                  const std::string& domain,
                                  dns::RecordType type,
                                  const std::string& desired) {
  std::vector<dns::Record> records;
  if (!provider_->ListRecords(zone_id, domain, type, &records)) {
    return false;
  }

  if (records.size() == 1 && records.front().value == desired) {
    LOG(INFO) << "DNS " << dns::RecordTypeToString(type) << " record for "
              << domain << " is already current";
    return true;
  }
  return provider_->UpsertRecord(zone_id, domain, type, desired, kDnsTtl);
}

bool DDNSManager::UpdateDomains(
    const std::vector<std::string>& domains,
    const std::vector<std::string>& addresses,
    const std::vector<std::string>& record_types) {
  if (domains.empty()) {
    return true;
  }
  if (domains.size() > kMaxDomainsPerReport) {
    LOG(ERROR) << "Client requested too many DDNS domains: "
               << domains.size();
    return false;
  }

  const auto selected_addresses = SelectPublicAddresses(addresses);
  const auto selected_types = ParseRecordTypes(record_types);
  if (selected_addresses.empty()) {
    LOG(WARNING) << "No public address available for requested DDNS update";
    return true;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_ || !provider_) {
    LOG(ERROR) << "Server-side DDNS manager is not initialized";
    return false;
  }

  bool all_success = true;
  std::set<std::string> unique_domains;
  for (const auto& value : domains) {
    const std::string domain = NormalizeDomain(value);
    if (!IsValidDomain(domain)) {
      LOG(ERROR) << "Ignoring invalid DDNS domain";
      all_success = false;
      continue;
    }
    if (!unique_domains.insert(domain).second) {
      continue;
    }

    std::string zone_id;
    const auto zone_it = domain_to_zone_id_.find(domain);
    if (zone_it != domain_to_zone_id_.end()) {
      zone_id = zone_it->second;
    } else {
      zone_id = provider_->GetZoneId(domain);
      if (zone_id.empty()) {
        all_success = false;
        continue;
      }
      domain_to_zone_id_[domain] = zone_id;
    }

    for (const auto type : selected_types) {
      const auto address_it = selected_addresses.find(type);
      if (address_it == selected_addresses.end()) {
        continue;
      }
      const std::string cache_key =
          domain + "|" + dns::RecordTypeToString(type);
      const auto cached = last_record_values_.find(cache_key);
      if (cached != last_record_values_.end() &&
          cached->second == address_it->second) {
        continue;
      }
      if (ReconcileRecord(zone_id, domain, type, address_it->second)) {
        last_record_values_[cache_key] = address_it->second;
      } else {
        all_success = false;
      }
    }
  }
  return all_success;
}

}  // namespace impl
}  // namespace tbox
