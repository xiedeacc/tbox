/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_IMPL_DDNS_MANAGER_H_
#define TBOX_IMPL_DDNS_MANAGER_H_

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "folly/Singleton.h"

namespace Aws {
namespace Route53 {
class Route53Client;
}  // namespace Route53
}  // namespace Aws

namespace tbox {
namespace impl {

/// @brief Manages Dynamic DNS updates for IPv6 via AWS Route53.
/// @details Thread-safe singleton that periodically checks IPv6
///          addresses and updates Route53 AAAA records when changes
///          are detected. Implements exponential backoff on failures.
class DDNSManager final {
 public:
  /// @brief Get singleton instance.
  /// @return Shared pointer to DDNSManager instance.
  static std::shared_ptr<DDNSManager> Instance();

  ~DDNSManager();

  /// @brief Initialize DDNS manager.
  /// @details Reads monitor domains from ConfigManager and uses hardcoded
  ///          settings for intervals and AWS configuration.
  /// @return True on success, false on failure.
  bool Init();

  /// @brief Start the background thread for periodic DDNS updates.
  void Start();

  /// @brief Stop the background thread.
  void Stop();

  /// @brief Check if the manager is running.
  /// @return True if the update thread is active, false otherwise.
  bool IsRunning() const { return running_.load(); }

  /// @brief Perform a single DDNS update check and update if needed.
  /// @return True on success or no update needed, false on failure.
  bool UpdateDNS();

  // Hardcoded configuration constants
  static constexpr int kDnsTtl = 60;               ///< DNS TTL for records
  static constexpr int kMaxBackoffSeconds = 3600;  ///< Max backoff (1 hour)
  static constexpr int kMinBackoffSeconds = 60;    ///< Min backoff (1 minute)
  static constexpr const char* kAwsRegion = "us-east-1";  ///< AWS region

 private:
  friend class folly::Singleton<DDNSManager>;
  DDNSManager();

  /// @brief Check if a specific IP is in the list.
  /// @param ip IP address to search for.
  /// @param list List of IP addresses to search in.
  /// @return True if ip is in list, false otherwise.
  static bool IsIPInList(const std::string& ip,
                         const std::vector<std::string>& list);

  /// @brief Get public IPv4 address from external service.
  /// @return Public IPv4 address, or empty string on failure.
  std::string GetPublicIPv4();

  /// @brief Check if IP address is private/loopback.
  /// @param ip IP address to check.
  /// @return True if private/loopback, false if public.
  static bool IsPrivateIP(const std::string& ip);

  /// @brief Get Route53 Hosted Zone ID for the domain.
  /// @param domain Domain name to look up.
  /// @return Hosted zone ID, or empty string on failure.
  std::string GetHostedZoneId(const std::string& domain);

  /// @brief Get current A records from Route53.
  /// @param hosted_zone_id Route53 hosted zone ID.
  /// @param domain Domain name to query.
  /// @return Vector of IPv4 addresses, or empty on failure.
  std::vector<std::string> GetCurrentRoute53ARecords(
      const std::string& hosted_zone_id, const std::string& domain);

  /// @brief Get current AAAA records from Route53.
  /// @param hosted_zone_id Route53 hosted zone ID.
  /// @param domain Domain name to query.
  /// @return Vector of IPv6 addresses, or empty on failure.
  std::vector<std::string> GetCurrentRoute53AAAARecords(
      const std::string& hosted_zone_id, const std::string& domain);

  /// @brief Update Route53 A record with new IPv4.
  /// @param hosted_zone_id Route53 hosted zone ID.
  /// @param domain Domain name to update.
  /// @param ipv4 New IPv4 address.
  /// @return True on success, false on failure.
  bool UpdateRoute53ARecord(const std::string& hosted_zone_id,
                            const std::string& domain, const std::string& ipv4);

  /// @brief Update Route53 AAAA record with new IPv6.
  /// @param hosted_zone_id Route53 hosted zone ID.
  /// @param domain Domain name to update.
  /// @param ipv6 New IPv6 address.
  /// @return True on success, false on failure.
  bool UpdateRoute53AAAARecord(const std::string& hosted_zone_id,
                               const std::string& domain,
                               const std::string& ipv6);

  /// @brief Delete Route53 record.
  /// @param hosted_zone_id Route53 hosted zone ID.
  /// @param domain Domain name.
  /// @param record_type Record type ("A" or "AAAA").
  /// @param ip IP address to delete.
  /// @return True on success, false on failure.
  bool DeleteRoute53Record(const std::string& hosted_zone_id,
                           const std::string& domain,
                           const std::string& record_type,
                           const std::string& ip);

  /// @brief The main loop that runs in the background thread.
  void UpdateLoop();

  // State
  std::vector<std::string> monitor_domains_;
  bool initialized_ = false;
  mutable std::mutex init_mutex_;

  // Route53 client
  std::unique_ptr<Aws::Route53::Route53Client> route53_client_;

  // Domain to hosted zone ID mapping
  std::map<std::string, std::string> domain_to_zone_id_;

  // Thread management
  std::atomic<bool> running_;
  std::atomic<bool> should_stop_;
  std::thread update_thread_;
  std::mutex mutex_;
  std::condition_variable cv_;

  // Configuration
  int check_interval_seconds_ = 30;  ///< Check interval from config

  // Exponential backoff state
  int consecutive_failures_ = 0;     ///< Consecutive failure count
  int current_backoff_seconds_ = 0;  ///< Current backoff duration

  /// @brief Calculate backoff delay based on failures.
  /// @return Backoff in seconds (exponential, capped at max).
  int CalculateBackoff();
};

}  // namespace impl
}  // namespace tbox

#endif  // TBOX_IMPL_DDNS_MANAGER_H_
