/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 ******************************************************************************/

#ifndef TBOX_TOOLS_SYNCER_FANOTIFY_WATCHER_H_
#define TBOX_TOOLS_SYNCER_FANOTIFY_WATCHER_H_

#include <fcntl.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "src/proto/syncer_config.pb.h"

namespace tbox {
namespace tools {
namespace syncer {

/// @brief Filesystem event types captured by fanotify.
enum class EventType {
  kCreate,
  kModify,
  kDelete,
  kMovedFrom,
  kMovedTo,
};

/// @brief Converts EventType to its string representation.
const char* EventTypeToString(EventType type);

/// @brief Parses a string back to EventType, returns false on failure.
bool StringToEventType(const std::string& str, EventType* type);

/**
 * @brief Monitors directories using Linux fanotify and journals events to disk.
 *
 * Uses FAN_REPORT_DIR_FID | FAN_REPORT_NAME (Linux 5.9+) to capture
 * directory-level events with filenames, avoiding per-subdirectory watches
 * that would be prohibitive for very large directory trees.
 */
class FanotifyWatcher {
 public:
  /**
   * @brief Construct a watcher.
   * @param config Syncer configuration containing sync pairs and journal path.
   * @param journal_mutex Mutex shared with SyncExecutor to protect journal I/O.
   */
  FanotifyWatcher(const tbox::proto::SyncerConfig& config,
                  std::mutex& journal_mutex);

  ~FanotifyWatcher();

  FanotifyWatcher(const FanotifyWatcher&) = delete;
  FanotifyWatcher& operator=(const FanotifyWatcher&) = delete;

  /**
   * @brief Initialize fanotify and mark source directories.
   * @return true on success.
   */
  bool Init();

  /**
   * @brief Start the background event-reading thread.
   */
  void Start();

  /**
   * @brief Stop the watcher and join the background thread.
   */
  void Stop();

 private:
  void EventLoop();
  void ProcessEvents(const char* buf, ssize_t len);
  std::string ResolveFidPath(int mount_fd,
                             const struct ::file_handle* file_handle);
  void AppendToJournal(int64_t timestamp_ms, EventType type,
                       const std::string& path);

  const tbox::proto::SyncerConfig& config_;
  std::mutex& journal_mutex_;

  int fanotify_fd_ = -1;
  // mount_fd -> source_dir mapping for path resolution
  std::unordered_map<int, std::string> mount_fds_;

  std::thread thread_;
  std::atomic<bool> running_{false};
};

}  // namespace syncer
}  // namespace tools
}  // namespace tbox

#endif  // TBOX_TOOLS_SYNCER_FANOTIFY_WATCHER_H_
