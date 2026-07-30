/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_IMPL_DNS_ROUTE53_PROVIDER_H_
#define TBOX_IMPL_DNS_ROUTE53_PROVIDER_H_

#include <memory>
#include <string>
#include <vector>

#include "src/impl/dns/dns_provider.h"

namespace Aws {
namespace Route53 {
class Route53Client;
}  // namespace Route53
}  // namespace Aws

namespace tbox {
namespace impl {
namespace dns {

/// @brief AWS Route53 implementation of the DNS backend interface.
/// @details Credentials and region are read from ConfigManager. When no
///          explicit credentials are configured the AWS default credential
///          chain is used.
class Route53Provider final : public DnsProvider {
 public:
  Route53Provider();
  ~Route53Provider() override;

  bool Init() override;

  std::string Name() const override { return "route53"; }

  std::string GetZoneId(const std::string& domain) override;

  bool ListRecords(const std::string& zone_id, const std::string& domain,
                   RecordType type, std::vector<Record>* records) override;

  bool UpsertRecord(const std::string& zone_id, const std::string& domain,
                    RecordType type, const std::string& value,
                    int ttl) override;

  bool DeleteRecord(const std::string& zone_id, const std::string& domain,
                    RecordType type, const std::string& value) override;

  /// @brief Default AWS region used when none is configured.
  static constexpr const char* kDefaultRegion = "us-east-1";

 private:
  /// @brief Submit a single change to a record set.
  /// @param zone_id Hosted zone identifier.
  /// @param domain Fully qualified domain name.
  /// @param type Record type to change.
  /// @param value Record value.
  /// @param ttl Time to live in seconds.
  /// @param upsert True to upsert, false to delete.
  /// @return True on success, false on failure.
  bool ApplyChange(const std::string& zone_id, const std::string& domain,
                   RecordType type, const std::string& value, int ttl,
                   bool upsert);

  std::unique_ptr<Aws::Route53::Route53Client> client_;
  bool sdk_initialized_ = false;
};

}  // namespace dns
}  // namespace impl
}  // namespace tbox

#endif  // TBOX_IMPL_DNS_ROUTE53_PROVIDER_H_
