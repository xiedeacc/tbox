/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_IMPL_DDNS_MANAGER_H_
#define TBOX_IMPL_DDNS_MANAGER_H_

#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "src/impl/dns/dns_provider.h"

namespace tbox {
namespace impl {

/// @brief Applies authenticated client address reports to DNS records.
/// @details The server owns all DNS provider credentials. Successful desired
///          values are cached so unchanged client reports do not call the DNS
///          provider again.
class DDNSManager final {
 public:
  static std::shared_ptr<DDNSManager> Instance();

  /// @brief Initialize the server-side DNS provider.
  bool Init();

  /// @brief Reconcile the requested domains with reported public addresses.
  /// @param domains Fully qualified domain names requested by the client.
  /// @param addresses Client-reported IPv4 and IPv6 addresses.
  /// @param record_types Optional A/AAAA selection; empty means both.
  /// @return True if every requested record is current or was updated.
  bool UpdateDomains(const std::vector<std::string>& domains,
                     const std::vector<std::string>& addresses,
                     const std::vector<std::string>& record_types);

  /// @brief Replace the provider and clear caches for an isolated unit test.
  void SetProviderForTesting(std::unique_ptr<dns::DnsProvider> provider);

  static constexpr int kDnsTtl = 60;
  static constexpr size_t kMaxDomainsPerReport = 32;

 private:
  DDNSManager() = default;

  static bool IsValidDomain(const std::string& domain);
  static std::string NormalizeDomain(const std::string& domain);
  static std::set<dns::RecordType> ParseRecordTypes(
      const std::vector<std::string>& record_types);
  static std::map<dns::RecordType, std::string> SelectPublicAddresses(
      const std::vector<std::string>& addresses);

  bool ReconcileRecord(const std::string& zone_id, const std::string& domain,
                       dns::RecordType type, const std::string& desired);

  bool initialized_ = false;
  std::unique_ptr<dns::DnsProvider> provider_;
  std::map<std::string, std::string> domain_to_zone_id_;
  std::map<std::string, std::string> last_record_values_;
  std::mutex mutex_;
};

}  // namespace impl
}  // namespace tbox

#endif  // TBOX_IMPL_DDNS_MANAGER_H_
