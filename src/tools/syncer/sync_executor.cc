/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 ******************************************************************************/

#include "src/tools/syncer/sync_executor.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "glog/logging.h"
#include "src/util/util.h"

namespace tbox {
namespace tools {
namespace syncer {

namespace fs = std::filesystem;

static constexpr uint32_t kDefaultSyncIntervalSeconds = 604800;

SyncExecutor::SyncExecutor(const tbox::proto::SyncerConfig& config,
                           std::mutex& journal_mutex)
    : config_(config), journal_mutex_(journal_mutex) {}

SyncExecutor::~SyncExecutor() { Stop(); }

void SyncExecutor::Start() {
  running_.store(true);
  thread_ = std::thread(&SyncExecutor::SyncLoop, this);
  LOG(INFO) << "SyncExecutor started, interval: "
            << (config_.sync_interval_seconds() > 0
                    ? config_.sync_interval_seconds()
                    : kDefaultSyncIntervalSeconds)
            << "s";
}

void SyncExecutor::Stop() {
  running_.store(false);
  cv_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
  LOG(INFO) << "SyncExecutor stopped";
}

void SyncExecutor::SyncLoop() {
  uint32_t interval = config_.sync_interval_seconds() > 0
                          ? config_.sync_interval_seconds()
                          : kDefaultSyncIntervalSeconds;

  while (running_.load()) {
    {
      std::unique_lock<std::mutex> lock(cv_mutex_);
      cv_.wait_for(lock, std::chrono::seconds(interval),
                   [this] { return !running_.load(); });
    }

    if (!running_.load()) break;

    LOG(INFO) << "Starting periodic sync";

    std::vector<JournalEntry> entries;
    if (!ReadJournal(&entries) || entries.empty()) {
      LOG(INFO) << "No journal entries to sync";
      continue;
    }

    DeduplicateEntries(&entries);
    LOG(INFO) << "Syncing " << entries.size() << " deduplicated entries";

    std::vector<JournalEntry> failed;
    for (const auto& entry : entries) {
      if (!SyncEntry(entry)) {
        failed.push_back(entry);
      }
    }

    if (failed.empty()) {
      ClearJournal();
      LOG(INFO) << "Sync completed successfully, journal cleared";
    } else {
      LOG(WARNING) << failed.size() << " entries failed, rewriting journal";
      WriteFailedEntries(failed);
    }
  }
}

bool SyncExecutor::ReadJournal(std::vector<JournalEntry>* entries) {
  std::lock_guard<std::mutex> lock(journal_mutex_);

  std::ifstream ifs(config_.event_journal_path());
  if (!ifs.is_open()) {
    LOG(WARNING) << "Journal file not found: " << config_.event_journal_path();
    return false;
  }

  std::string line;
  while (std::getline(ifs, line)) {
    if (line.empty()) continue;

    std::vector<std::string> parts;
    tbox::util::Util::Split(line, "|", &parts, false);
    if (parts.size() < 3) {
      LOG(WARNING) << "Malformed journal line: " << line;
      continue;
    }

    JournalEntry entry;
    if (!tbox::util::Util::ToInt(parts[0], &entry.timestamp_ms)) {
      LOG(WARNING) << "Invalid timestamp in journal: " << parts[0];
      continue;
    }
    if (!StringToEventType(parts[1], &entry.event_type)) {
      LOG(WARNING) << "Invalid event type in journal: " << parts[1];
      continue;
    }
    // Path may contain '|', rejoin remaining parts
    entry.path = parts[2];
    for (size_t i = 3; i < parts.size(); ++i) {
      entry.path += "|";
      entry.path += parts[i];
    }

    entries->push_back(std::move(entry));
  }

  return true;
}

void SyncExecutor::DeduplicateEntries(std::vector<JournalEntry>* entries) {
  // Keep last event per path (highest timestamp wins)
  std::unordered_map<std::string, size_t> latest;
  for (size_t i = 0; i < entries->size(); ++i) {
    auto it = latest.find((*entries)[i].path);
    if (it == latest.end() ||
        (*entries)[i].timestamp_ms > (*entries)[it->second].timestamp_ms) {
      latest[(*entries)[i].path] = i;
    }
  }

  std::vector<JournalEntry> deduped;
  deduped.reserve(latest.size());
  for (auto& [path, idx] : latest) {
    deduped.push_back(std::move((*entries)[idx]));
  }
  *entries = std::move(deduped);
}

bool SyncExecutor::SyncEntry(const JournalEntry& entry) {
  const tbox::proto::SyncPair* pair = FindSyncPair(entry.path);
  if (pair == nullptr) {
    LOG(WARNING) << "No sync pair found for path: " << entry.path;
    return true;
  }

  fs::path src_base(pair->source_dir());
  fs::path abs_path(entry.path);

  std::error_code ec;
  fs::path rel = fs::relative(abs_path, src_base, ec);
  if (ec) {
    LOG(ERROR) << "Failed to compute relative path for " << entry.path
               << " against " << pair->source_dir() << ": " << ec.message();
    return false;
  }

  bool all_ok = true;

  for (const auto& dest : pair->dest_dirs()) {
    fs::path dest_path = fs::path(dest) / rel;

    switch (entry.event_type) {
      case EventType::kCreate:
      case EventType::kModify:
      case EventType::kMovedTo: {
        if (!fs::exists(abs_path, ec)) {
          LOG(WARNING) << "Source file no longer exists: " << abs_path;
          break;
        }

        fs::create_directories(dest_path.parent_path(), ec);
        if (ec) {
          LOG(ERROR) << "Failed to create dest directory "
                     << dest_path.parent_path() << ": " << ec.message();
          all_ok = false;
          continue;
        }

        if (fs::is_directory(abs_path, ec)) {
          fs::create_directories(dest_path, ec);
          if (ec) {
            LOG(ERROR) << "Failed to create directory " << dest_path << ": "
                       << ec.message();
            all_ok = false;
          }
        } else {
          fs::copy(abs_path, dest_path,
                   fs::copy_options::overwrite_existing, ec);
          if (ec) {
            LOG(ERROR) << "Failed to copy " << abs_path << " -> " << dest_path
                       << ": " << ec.message();
            all_ok = false;
          } else {
            LOG(INFO) << "Synced: " << abs_path << " -> " << dest_path;
          }
        }
        break;
      }

      case EventType::kDelete:
      case EventType::kMovedFrom: {
        if (fs::exists(dest_path, ec)) {
          fs::remove_all(dest_path, ec);
          if (ec) {
            LOG(ERROR) << "Failed to remove " << dest_path << ": "
                       << ec.message();
            all_ok = false;
          } else {
            LOG(INFO) << "Removed: " << dest_path;
          }
        }
        break;
      }
    }
  }

  return all_ok;
}

bool SyncExecutor::WriteFailedEntries(
    const std::vector<JournalEntry>& entries) {
  std::lock_guard<std::mutex> lock(journal_mutex_);

  std::ofstream ofs(config_.event_journal_path(),
                    std::ios::out | std::ios::trunc);
  if (!ofs.is_open()) {
    LOG(ERROR) << "Failed to open journal for rewrite: "
               << config_.event_journal_path();
    return false;
  }

  for (const auto& entry : entries) {
    ofs << entry.timestamp_ms << "|" << EventTypeToString(entry.event_type)
        << "|" << entry.path << "\n";
  }

  return true;
}

void SyncExecutor::ClearJournal() {
  std::lock_guard<std::mutex> lock(journal_mutex_);

  std::ofstream ofs(config_.event_journal_path(),
                    std::ios::out | std::ios::trunc);
  if (!ofs.is_open()) {
    LOG(ERROR) << "Failed to truncate journal: "
               << config_.event_journal_path();
  }
}

const tbox::proto::SyncPair* SyncExecutor::FindSyncPair(
    const std::string& path) const {
  const tbox::proto::SyncPair* best = nullptr;
  size_t best_len = 0;

  for (const auto& pair : config_.sync_pairs()) {
    const std::string& src = pair.source_dir();
    if (path.size() >= src.size() &&
        path.compare(0, src.size(), src) == 0 &&
        (path.size() == src.size() || path[src.size()] == '/') &&
        src.size() > best_len) {
      best = &pair;
      best_len = src.size();
    }
  }

  return best;
}

}  // namespace syncer
}  // namespace tools
}  // namespace tbox
