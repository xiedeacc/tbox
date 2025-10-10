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

  // Configuration
  int report_interval_seconds_ = 30;
  int login_retry_seconds_ = 60;
  bool initialized_ = false;
  mutable std::mutex init_mutex_;

  // Connection components
  std::shared_ptr<grpc::Channel> channel_;
  std::shared_ptr<tbox::proto::TBOXService::Stub> stub_;

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
  static constexpr int kHealthCheckTimeoutSeconds = 5;
};

}  // namespace client
}  // namespace tbox

#endif  // TBOX_CLIENT_REPORT_MANAGER_H_
