/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_CLIENT_REPORT_MANAGER_H_
#define TBOX_CLIENT_REPORT_MANAGER_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "folly/Singleton.h"
#include "grpcpp/grpcpp.h"
#include "src/proto/service.grpc.pb.h"

namespace tbox {
namespace client {

/// @brief Manages periodic IP address reporting to server.
/// @details Thread-safe singleton that handles authentication,
///          connection health checking, and periodic IP reporting.
class ReportManager final {
 public:
  /// @brief Get singleton instance.
  /// @return Shared pointer to ReportManager instance.
  static std::shared_ptr<ReportManager> Instance();

  ~ReportManager();

  /// @brief Initialize with gRPC channel and configuration.
  /// @param channel Shared pointer to gRPC channel.
  /// @param stub Shared pointer to TBOXService stub.
  /// @param report_interval_seconds Interval between reports.
  /// @param login_retry_seconds Interval between login retries.
  /// @return True on success, false on failure.
  bool Init(std::shared_ptr<grpc::Channel> channel,
            std::shared_ptr<tbox::proto::TBOXService::Stub> stub,
            int report_interval_seconds = 30, int login_retry_seconds = 60);

  /// @brief Start the background reporting thread.
  void Start();

  /// @brief Stop the background reporting thread.
  void Stop();

  /// @brief Check if the manager is running.
  /// @return True if the reporting thread is active, false otherwise.
  bool IsRunning() const { return running_.load(); }

  /// @brief Perform a single IP report.
  /// @return True on success, false on failure.
  bool ReportClientIP();

 private:
  friend class folly::Singleton<ReportManager>;
  ReportManager();

  /// @brief The main loop that runs in the background thread.
  void ReportingLoop();

  /// @brief Check if gRPC connection is healthy.
  /// @return True if connection is healthy, false otherwise.
  bool IsConnectionHealthy();

  /// @brief Attempt to reconnect to the gRPC server.
  /// @return True if reconnection successful, false otherwise.
  bool Reconnect();

  /// @brief Check if IP addresses have changed or heartbeat is due.
  /// @param current_ips Current IP addresses.
  /// @return True if report is needed, false otherwise.
  bool ShouldReport(const std::vector<std::string>& current_ips);

  /// @brief Get public IPv4 address from server.
  /// @return Public IPv4 address, or empty string on failure.
  std::string GetPublicIPv4();

  /// @brief Get public IPv6 address from server.
  /// @return Public IPv6 address, or empty string on failure.
  std::string GetPublicIPv6();

  /// @brief Create a gRPC stub that forces IPv4 connection.
  /// @return Stub connected via IPv4, or nullptr on failure.
  std::shared_ptr<tbox::proto::TBOXService::Stub> CreateIPv4Stub();

  /// @brief Create a gRPC stub that forces IPv6 connection.
  /// @return Stub connected via IPv6, or nullptr on failure.
  std::shared_ptr<tbox::proto::TBOXService::Stub> CreateIPv6Stub();

  // Configuration
  int report_interval_seconds_ = 30;
  int login_retry_seconds_ = 60;
  bool initialized_ = false;
  mutable std::mutex init_mutex_;

  // Connection components
  std::shared_ptr<grpc::Channel> channel_;
  std::shared_ptr<tbox::proto::TBOXService::Stub> stub_;
  std::shared_ptr<tbox::proto::TBOXService::Stub> ipv4_stub_;
  std::shared_ptr<tbox::proto::TBOXService::Stub> ipv6_stub_;

  // Thread management
  std::atomic<bool> running_;
  std::atomic<bool> should_stop_;
  std::thread reporting_thread_;
  std::mutex mutex_;
  std::condition_variable cv_;

  // Connection health tracking
  int64_t last_successful_op_millis_ = 0;  // Unix timestamp in milliseconds
  std::atomic<bool> connection_healthy_;
  mutable std::mutex connection_mutex_;
  static constexpr int kHealthCheckTimeoutSeconds = 15;

  // IP change detection
  std::vector<std::string> last_reported_ips_;
  int64_t last_report_time_millis_ = 0;
  mutable std::mutex ip_tracking_mutex_;
  static constexpr int kHeartbeatIntervalSeconds = 3600;  // 1 hour
};

}  // namespace client
}  // namespace tbox

#endif  // TBOX_CLIENT_REPORT_MANAGER_H_
