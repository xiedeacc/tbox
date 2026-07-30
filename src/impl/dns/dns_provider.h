/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_IMPL_DNS_DNS_PROVIDER_H_
#define TBOX_IMPL_DNS_DNS_PROVIDER_H_

#include <memory>
#include <string>
#include <vector>

namespace tbox {
namespace impl {
namespace dns {

/// @brief Address record types handled by the DDNS manager.
enum class RecordType {
  kA,     ///< IPv4 address record.
  kAAAA,  ///< IPv6 address record.
};

/// @brief Convert a record type to its textual DNS representation.
/// @param type Record type to convert.
/// @return "A" or "AAAA".
const char* RecordTypeToString(RecordType type);

/// @brief A single address record returned by a backend.
struct Record {
  std::string id;     ///< Backend record identifier, empty when unsupported.
  std::string name;   ///< Fully qualified record name without trailing dot.
  std::string value;  ///< Record value, an IPv4 or IPv6 literal.
  RecordType type = RecordType::kA;  ///< Record type.
  int ttl = 0;                       ///< Time to live in seconds.
};

/// @brief Backend independent DNS record management interface.
/// @details Each supported DNS hosting provider implements this interface in
///          its own translation unit. Implementations must be safe to call
///          from the single DDNS worker thread; they are not required to be
///          thread-safe beyond that.
class DnsProvider {
 public:
  virtual ~DnsProvider() = default;

  /// @brief Prepare the backend for use.
  /// @details Validates credentials and initialises any backend SDK. Must be
  ///          called once before any other method.
  /// @return True on success, false on failure.
  virtual bool Init() = 0;

  /// @brief Get the backend identifier.
  /// @return Backend name such as "route53" or "cloudflare".
  virtual std::string Name() const = 0;

  /// @brief Resolve the backend zone identifier that owns a domain.
  /// @details Implementations should return a configured zone identifier
  ///          without a remote call when one is available.
  /// @param domain Fully qualified domain name.
  /// @return Zone identifier, or an empty string on failure.
  virtual std::string GetZoneId(const std::string& domain) = 0;

  /// @brief List the address records of a given type for a domain.
  /// @param zone_id Backend zone identifier.
  /// @param domain Fully qualified domain name.
  /// @param type Record type to list.
  /// @param records Output vector, cleared before use.
  /// @return True on success, false on failure.
  virtual bool ListRecords(const std::string& zone_id,
                           const std::string& domain, RecordType type,
                           std::vector<Record>* records) = 0;

  /// @brief Create or replace an address record.
  /// @details The record set for the given name and type ends up holding
  ///          exactly the supplied value.
  /// @param zone_id Backend zone identifier.
  /// @param domain Fully qualified domain name.
  /// @param type Record type to write.
  /// @param value IPv4 or IPv6 literal to publish.
  /// @param ttl Time to live in seconds.
  /// @return True on success, false on failure.
  virtual bool UpsertRecord(const std::string& zone_id,
                            const std::string& domain, RecordType type,
                            const std::string& value, int ttl) = 0;

  /// @brief Delete a single address record.
  /// @param zone_id Backend zone identifier.
  /// @param domain Fully qualified domain name.
  /// @param type Record type to delete.
  /// @param value Exact record value to remove.
  /// @return True on success, false on failure.
  virtual bool DeleteRecord(const std::string& zone_id,
                            const std::string& domain, RecordType type,
                            const std::string& value) = 0;
};

/// @brief Build the DNS backend selected by the active configuration.
/// @details Reads the dns_provider field from ConfigManager and constructs the
///          matching implementation. The returned backend is not initialised;
///          the caller must invoke Init().
/// @return Owned backend instance, or nullptr when the name is unknown.
std::unique_ptr<DnsProvider> CreateDnsProvider();

/// @brief Build a DNS backend by name.
/// @param name Backend name such as "route53" or "cloudflare".
/// @return Owned backend instance, or nullptr when the name is unknown.
std::unique_ptr<DnsProvider> CreateDnsProvider(const std::string& name);

}  // namespace dns
}  // namespace impl
}  // namespace tbox

#endif  // TBOX_IMPL_DNS_DNS_PROVIDER_H_
