/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/impl/dns/cloudflare_provider.h"

#include <map>
#include <string>
#include <vector>

#include "curl/curl.h"
#include "folly/json.h"
#include "src/common/logging.h"
#include "src/impl/config_manager.h"

namespace tbox {
namespace impl {
namespace dns {
namespace {

/// @brief libcurl write callback accumulating the response body.
/// @param ptr Pointer to the received bytes.
/// @param size Size of a single element.
/// @param nmemb Element count.
/// @param userdata Destination string.
/// @return Number of bytes consumed.
size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  const size_t total = size * nmemb;
  static_cast<std::string*>(userdata)->append(ptr, total);
  return total;
}

/// @brief Strip a trailing dot so names match Cloudflare's representation.
/// @param domain Domain name to normalise.
/// @return Domain name without a trailing dot.
std::string ToRelativeName(const std::string& domain) {
  if (!domain.empty() && domain.back() == '.') {
    return domain.substr(0, domain.size() - 1);
  }
  return domain;
}

/// @brief Build the list of parent suffixes of a domain, most specific first.
/// @param domain Fully qualified domain name.
/// @return Candidate zone names, for example {"a.b.com", "b.com"}.
std::vector<std::string> ZoneCandidates(const std::string& domain) {
  std::vector<std::string> candidates;
  const std::string name = ToRelativeName(domain);
  size_t pos = 0;
  while (pos < name.size()) {
    const std::string suffix = name.substr(pos);
    // A zone always has at least one dot, so skip a bare TLD.
    if (suffix.find('.') == std::string::npos) {
      break;
    }
    candidates.push_back(suffix);
    const size_t next = name.find('.', pos);
    if (next == std::string::npos) {
      break;
    }
    pos = next + 1;
  }
  return candidates;
}

}  // namespace

CloudflareProvider::CloudflareProvider() = default;

CloudflareProvider::~CloudflareProvider() = default;

bool CloudflareProvider::Init() {
  auto config_manager = util::ConfigManager::Instance();
  api_token_ = config_manager->CloudflareApiToken();
  configured_zone_id_ = config_manager->CloudflareZoneId();

  if (api_token_.empty()) {
    LOG(ERROR) << "Cloudflare backend requires cloudflare_api_token";
    return false;
  }

  LOG(INFO) << "Cloudflare backend initialized"
            << (configured_zone_id_.empty()
                    ? ", zone will be resolved per domain"
                    : ", using configured zone id");
  return true;
}

bool CloudflareProvider::Call(const std::string& method,
                              const std::string& path, const std::string& body,
                              std::string* result) {
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    LOG(ERROR) << "Failed to initialize curl handle";
    return false;
  }

  const std::string url = std::string(kApiBase) + path;
  std::string response;

  // The token travels in a header, never on the command line or in the URL.
  curl_slist* headers = nullptr;
  headers = curl_slist_append(
      headers, ("Authorization: Bearer " + api_token_).c_str());
  headers = curl_slist_append(headers, "Content-Type: application/json");

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, kTimeoutSeconds);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  if (!body.empty()) {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                     static_cast<int64_t>(body.size()));
  }

  const CURLcode code = curl_easy_perform(curl);
  int64_t http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (code != CURLE_OK) {
    LOG(ERROR) << "Cloudflare request failed: " << curl_easy_strerror(code);
    return false;
  }

  try {
    const folly::dynamic parsed = folly::parseJson(response);
    const auto* success = parsed.get_ptr("success");
    if (success == nullptr || !success->isBool() || !success->asBool()) {
      std::string detail;
      const auto* errors = parsed.get_ptr("errors");
      if (errors != nullptr) {
        detail = folly::toJson(*errors);
      }
      LOG(ERROR) << "Cloudflare API error, http " << http_code << ": "
                 << detail;
      return false;
    }
    if (result != nullptr) {
      const auto* payload = parsed.get_ptr("result");
      *result = payload == nullptr ? "" : folly::toJson(*payload);
    }
  } catch (const std::exception& e) {
    LOG(ERROR) << "Failed to parse Cloudflare response: " << e.what();
    return false;
  }
  return true;
}

std::string CloudflareProvider::GetZoneId(const std::string& domain) {
  if (!configured_zone_id_.empty()) {
    return configured_zone_id_;
  }

  const auto cached = domain_to_zone_id_.find(domain);
  if (cached != domain_to_zone_id_.end()) {
    return cached->second;
  }

  for (const auto& candidate : ZoneCandidates(domain)) {
    std::string result;
    if (!Call("GET", "/zones?name=" + candidate, "", &result)) {
      continue;
    }
    try {
      const folly::dynamic zones = folly::parseJson(result);
      if (!zones.isArray() || zones.empty()) {
        continue;
      }
      const auto* id = zones[0].get_ptr("id");
      if (id == nullptr || !id->isString()) {
        continue;
      }
      const std::string zone_id = id->asString();
      domain_to_zone_id_[domain] = zone_id;
      LOG(INFO) << "Resolved Cloudflare zone for " << domain << ": "
                << candidate;
      return zone_id;
    } catch (const std::exception& e) {
      LOG(ERROR) << "Failed to parse Cloudflare zone list: " << e.what();
    }
  }

  LOG(ERROR) << "Cloudflare zone not found for domain: " << domain;
  return "";
}

bool CloudflareProvider::FetchRecords(const std::string& zone_id,
                                      const std::string& domain,
                                      RecordType type,
                                      std::vector<CloudflareRecord>* records) {
  records->clear();

  const std::string path = "/zones/" + zone_id + "/dns_records?type=" +
                           RecordTypeToString(type) + "&name=" +
                           ToRelativeName(domain);
  std::string result;
  if (!Call("GET", path, "", &result)) {
    return false;
  }

  try {
    const folly::dynamic parsed = folly::parseJson(result);
    if (!parsed.isArray()) {
      return false;
    }
    for (const auto& entry : parsed) {
      CloudflareRecord record;
      const auto* id = entry.get_ptr("id");
      const auto* content = entry.get_ptr("content");
      if (id == nullptr || content == nullptr) {
        continue;
      }
      record.id = id->asString();
      record.content = content->asString();
      const auto* ttl = entry.get_ptr("ttl");
      if (ttl != nullptr && ttl->isInt()) {
        record.ttl = static_cast<int>(ttl->asInt());
      }
      const auto* proxied = entry.get_ptr("proxied");
      if (proxied != nullptr && proxied->isBool()) {
        record.proxied = proxied->asBool();
      }
      records->push_back(record);
    }
  } catch (const std::exception& e) {
    LOG(ERROR) << "Failed to parse Cloudflare records: " << e.what();
    return false;
  }
  return true;
}

bool CloudflareProvider::ListRecords(const std::string& zone_id,
                                     const std::string& domain, RecordType type,
                                     std::vector<Record>* records) {
  records->clear();

  std::vector<CloudflareRecord> raw;
  if (!FetchRecords(zone_id, domain, type, &raw)) {
    return false;
  }

  for (const auto& entry : raw) {
    Record item;
    item.id = entry.id;
    item.name = ToRelativeName(domain);
    item.type = type;
    item.value = entry.content;
    item.ttl = entry.ttl;
    records->push_back(item);
  }
  return true;
}

bool CloudflareProvider::UpsertRecord(const std::string& zone_id,
                                      const std::string& domain,
                                      RecordType type,
                                      const std::string& value, int ttl) {
  std::vector<CloudflareRecord> existing;
  if (!FetchRecords(zone_id, domain, type, &existing)) {
    return false;
  }

  // Preserve a deliberate proxy setting; new records stay unproxied so the
  // published address is the origin itself.
  const bool proxied = existing.empty() ? false : existing.front().proxied;
  // Cloudflare rejects an explicit TTL on proxied records.
  const int effective_ttl = proxied ? 1 : ttl;

  folly::dynamic payload = folly::dynamic::object;
  payload["type"] = RecordTypeToString(type);
  payload["name"] = ToRelativeName(domain);
  payload["content"] = value;
  payload["ttl"] = effective_ttl;
  payload["proxied"] = proxied;
  const std::string body = folly::toJson(payload);

  bool ok = false;
  if (existing.empty()) {
    ok = Call("POST", "/zones/" + zone_id + "/dns_records", body, nullptr);
  } else {
    ok = Call("PUT",
              "/zones/" + zone_id + "/dns_records/" + existing.front().id, body,
              nullptr);
    // Collapse any extra records so the set holds exactly one value.
    for (size_t i = 1; i < existing.size(); ++i) {
      Call("DELETE", "/zones/" + zone_id + "/dns_records/" + existing[i].id, "",
           nullptr);
    }
  }

  if (!ok) {
    LOG(ERROR) << "Failed to upsert Cloudflare " << RecordTypeToString(type)
               << " record for " << domain;
    return false;
  }

  LOG(INFO) << "Successfully updated Cloudflare " << RecordTypeToString(type)
            << " record: " << domain << " -> " << value;
  return true;
}

bool CloudflareProvider::DeleteRecord(const std::string& zone_id,
                                      const std::string& domain,
                                      RecordType type,
                                      const std::string& value) {
  std::vector<CloudflareRecord> existing;
  if (!FetchRecords(zone_id, domain, type, &existing)) {
    return false;
  }

  for (const auto& entry : existing) {
    if (entry.content != value) {
      continue;
    }
    if (!Call("DELETE", "/zones/" + zone_id + "/dns_records/" + entry.id, "",
              nullptr)) {
      LOG(ERROR) << "Failed to delete Cloudflare " << RecordTypeToString(type)
                 << " record for " << domain;
      return false;
    }
    LOG(WARNING) << "Deleted Cloudflare " << RecordTypeToString(type)
                 << " record: " << domain << " -> " << value;
    return true;
  }

  // Already absent, treat as success so callers stay idempotent.
  return true;
}

}  // namespace dns
}  // namespace impl
}  // namespace tbox
