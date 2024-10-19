/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_UTIL_LOG_CLEANER_H
#define TBOX_UTIL_LOG_CLEANER_H

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "folly/Singleton.h"
#include "glog/logging.h"

namespace fs = std::filesystem;

namespace tbox {
namespace util {

class LogCleaner {
 private:
  friend class folly::Singleton<LogCleaner>;
  LogCleaner() = default;

 public:
  static std::shared_ptr<LogCleaner> Instance();

  bool Init(const std::string log_dir, const int64_t max_size) {
    this->log_dir = log_dir;
    this->max_size = max_size;
    clean_task_ = std::thread(std::bind(&LogCleaner::Clean, this));
    return true;
  }

  void Stop() {
    stop_ = true;
    cv_.notify_all();
  }

  ~LogCleaner() {
    if (clean_task_.joinable()) {
      clean_task_.join();
    }
  }

  void Clean() {
    while (!stop_) {
      if (stop_) {
        break;
      }
      if (WaitUntil2AM()) {
        break;
      }
      CleanLogs();
      std::unique_lock<std::mutex> locker(mu_);
      cv_.wait_for(locker, std::chrono::hours(2), [this] { return stop_; });
    }
    LOG(INFO) << "Clean task exist";
  }

 private:
  void CleanLogs() {
    std::vector<fs::directory_entry> log_files;
    for (const auto& entry : fs::directory_iterator(log_dir)) {
      if (entry.is_regular_file()) {
        log_files.push_back(entry);
      }
    }

    std::sort(log_files.begin(), log_files.end(),
              [](const fs::directory_entry& a, const fs::directory_entry& b) {
                return fs::last_write_time(a) < fs::last_write_time(b);
              });

    int64_t total_size = CalculateTotalSize(log_files);
    int32_t removed_file_num = 0;

    while (total_size > max_size && !log_files.empty()) {
      total_size -= fs::file_size(log_files.front());
      fs::remove(log_files.front());
      log_files.erase(log_files.begin());
      ++removed_file_num;
    }
    LOG(INFO) << removed_file_num << " log file cleaned";
  }

  int64_t CalculateTotalSize(const std::vector<fs::directory_entry>& files) {
    int64_t total_size = 0;
    for (const auto& file : files) {
      total_size += fs::file_size(file);
    }
    return total_size;
  }

  bool WaitUntil2AM() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm* parts = std::localtime(&now_c);

    parts->tm_hour = 2;
    parts->tm_min = 0;
    parts->tm_sec = 0;
    auto next_wake_time =
        std::chrono::system_clock::from_time_t(std::mktime(parts));

    if (next_wake_time < now) {
      next_wake_time += std::chrono::hours(24);
    }

    std::unique_lock<std::mutex> locker(mu_);
    return cv_.wait_until(locker, next_wake_time, [this] { return stop_; });
  }

 private:
  std::string log_dir;
  int64_t max_size;
  bool stop_ = false;
  std::thread clean_task_;

  std::mutex mu_;
  std::condition_variable cv_;
};

}  // namespace util
}  // namespace tbox

#endif  // TBOX_UTIL_LOG_CLEANER_H
