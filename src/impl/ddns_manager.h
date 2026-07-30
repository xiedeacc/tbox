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

#include "src/impl/dns/dns_provider.h"

namespace tbox {
namespace impl {

/// @brief Manages Dynamic DNS updates for IPv4 and IPv6.
/// @details Thread-safe singleton that periodically checks the host addresses
///          and updates A and AAAA records whenever they change. The DNS
///          hosting backend is pluggable; see src/impl/dns for the supported
///          implementations. Failures are retried with exponential backoff.
class DDNSManager final {
 public:
  /// @brief Get singleton instance.
  /// @return Shared pointer to DDNSManager instance.
  static std::shared_ptr<DDNSManager> Instance();

  ~DDNSManager();

  /// @brief Initialize DDNS manager.
  /// @details Reads monitor domains and the DNS backend selection from
  ///          ConfigManager, then resolves the zone of every monitored domain.
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

 private:
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

  /// @brief Reconcile one record type of one domain against a desired value.
  /// @param zone_id Backend zone identifier.
  /// @param domain Domain name to reconcile.
  /// @param type Record type to reconcile.
  /// @param desired Desired address, empty to only prune private entries.
  /// @param log_buffer Buffer collecting human readable progress lines.
  /// @param updated Set to true when a change was applied.
  /// @return True on success, false when any operation failed.
  bool ReconcileRecord(const std::string& zone_id, const std::string& domain,
                       dns::RecordType type, const std::string& desired,
                       std::vector<std::string>* log_buffer, bool* updated);

  /// @brief The main loop that runs in the background thread.
  void UpdateLoop();

  // State
  std::vector<std::string> monitor_domains_;
  bool initialized_ = false;
  mutable std::mutex init_mutex_;

  // Pluggable DNS backend
  std::unique_ptr<dns::DnsProvider> provider_;

  // Domain to backend zone ID mapping
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
