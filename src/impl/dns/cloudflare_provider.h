/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_IMPL_DNS_CLOUDFLARE_PROVIDER_H_
#define TBOX_IMPL_DNS_CLOUDFLARE_PROVIDER_H_

#include <map>
#include <string>
#include <vector>

#include "src/impl/dns/dns_provider.h"

namespace tbox {
namespace impl {
namespace dns {

/// @brief Cloudflare implementation of the DNS backend interface.
/// @details Talks to the Cloudflare v4 REST API over libcurl using a scoped
///          API token. The token requires the Zone:DNS:Edit permission on
///          every managed zone. Records are created unproxied so that DDNS
///          answers expose the origin address directly; the proxy flag of an
///          existing record is preserved on update.
class CloudflareProvider final : public DnsProvider {
 public:
  CloudflareProvider();
  ~CloudflareProvider() override;

  bool Init() override;

  std::string Name() const override { return "cloudflare"; }

  std::string GetZoneId(const std::string& domain) override;

  bool ListRecords(const std::string& zone_id, const std::string& domain,
                   RecordType type, std::vector<Record>* records) override;

  bool UpsertRecord(const std::string& zone_id, const std::string& domain,
                    RecordType type, const std::string& value,
                    int ttl) override;

  bool DeleteRecord(const std::string& zone_id, const std::string& domain,
                    RecordType type, const std::string& value) override;

  /// @brief Cloudflare API base endpoint.
  static constexpr const char* kApiBase =
      "https://api.cloudflare.com/client/v4";

  /// @brief Network timeout applied to every API call, in seconds.
  static constexpr int64_t kTimeoutSeconds = 15;

 private:
  /// @brief A record as returned by the Cloudflare API.
  struct CloudflareRecord {
    std::string id;       ///< Cloudflare record identifier.
    std::string content;  ///< Record value.
    int ttl = 1;          ///< Time to live, 1 means automatic.
    bool proxied = false;  ///< Whether Cloudflare proxies this record.
  };

  /// @brief Perform an authenticated Cloudflare API request.
  /// @param method HTTP method such as "GET" or "POST".
  /// @param path Path below the API base, must start with a slash.
  /// @param body Request body, empty when not applicable.
  /// @param result Parsed "result" member of the response envelope.
  /// @return True when the API reported success, false otherwise.
  bool Call(const std::string& method, const std::string& path,
            const std::string& body, std::string* result);

  /// @brief Fetch the records matching a name and type.
  /// @param zone_id Cloudflare zone identifier.
  /// @param domain Fully qualified domain name.
  /// @param type Record type to fetch.
  /// @param records Output vector, cleared before use.
  /// @return True on success, false on failure.
  bool FetchRecords(const std::string& zone_id, const std::string& domain,
                    RecordType type, std::vector<CloudflareRecord>* records);

  std::string api_token_;
  std::string configured_zone_id_;
  std::map<std::string, std::string> domain_to_zone_id_;
};

}  // namespace dns
}  // namespace impl
}  // namespace tbox

#endif  // TBOX_IMPL_DNS_CLOUDFLARE_PROVIDER_H_
