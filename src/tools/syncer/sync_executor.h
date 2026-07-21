/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 ******************************************************************************/

#ifndef TBOX_TOOLS_SYNCER_SYNC_EXECUTOR_H_
#define TBOX_TOOLS_SYNCER_SYNC_EXECUTOR_H_

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "src/proto/syncer_config.pb.h"
#include "src/tools/syncer/fanotify_watcher.h"

namespace tbox {
namespace tools {
namespace syncer {

/// @brief A single parsed journal entry.
struct JournalEntry {
  int64_t timestamp_ms;
  EventType event_type;
  std::string path;
};

/**
 * @brief Periodically reads the event journal, syncs files to destination
 *        directories, and clears processed entries.
 *
 * Deduplicates events on the same path (last event wins). For CREATE/MODIFY/
 * MOVED_TO, copies the file. For DELETE/MOVED_FROM, removes from destinations.
 */
class SyncExecutor {
 public:
  /**
   * @brief Construct a sync executor.
   * @param config Syncer configuration.
   * @param journal_mutex Mutex shared with FanotifyWatcher.
   */
  SyncExecutor(const tbox::proto::SyncerConfig& config,
               std::mutex& journal_mutex);

  ~SyncExecutor();

  SyncExecutor(const SyncExecutor&) = delete;
  SyncExecutor& operator=(const SyncExecutor&) = delete;

  /**
   * @brief Start the periodic sync thread.
   */
  void Start();

  /**
   * @brief Stop the sync thread and join.
   */
  void Stop();

 private:
  void SyncLoop();
  bool ReadJournal(std::vector<JournalEntry>* entries);
  void DeduplicateEntries(std::vector<JournalEntry>* entries);
  bool SyncEntry(const JournalEntry& entry);
  bool WriteFailedEntries(const std::vector<JournalEntry>& entries);
  void ClearJournal();

  /// @brief Find the SyncPair whose source_dir is a prefix of the given path.
  const tbox::proto::SyncPair* FindSyncPair(const std::string& path) const;

  const tbox::proto::SyncerConfig& config_;
  std::mutex& journal_mutex_;

  std::thread thread_;
  std::atomic<bool> running_{false};
  std::mutex cv_mutex_;
  std::condition_variable cv_;
};

}  // namespace syncer
}  // namespace tools
}  // namespace tbox

#endif  // TBOX_TOOLS_SYNCER_SYNC_EXECUTOR_H_
