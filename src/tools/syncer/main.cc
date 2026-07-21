/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 ******************************************************************************/

#include <signal.h>

#include <atomic>
#include <filesystem>
#include <mutex>

#include "folly/init/Init.h"
#include "glog/logging.h"
#include "src/proto/syncer_config.pb.h"
#include "src/tools/syncer/fanotify_watcher.h"
#include "src/tools/syncer/sync_executor.h"
#include "src/util/util.h"

namespace {

std::atomic<bool> g_shutdown{false};

void SignalHandler(int sig) {
  LOG(INFO) << "Received signal " << sig << ", shutting down";
  g_shutdown.store(true);
}

}  // namespace

int main(int argc, char* argv[]) {
  folly::Init init(&argc, &argv, false);
  google::EnableLogCleaner(7);
  google::SetStderrLogging(google::GLOG_INFO);

  if (argc < 2) {
    LOG(FATAL) << "Usage: syncer <config_json_path>";
  }

  std::string config_path = argv[1];
  std::string content;
  if (!tbox::util::Util::LoadSmallFile(config_path, &content)) {
    LOG(FATAL) << "Failed to load config file: " << config_path;
  }

  tbox::proto::SyncerConfig config;
  if (!tbox::util::Util::JsonToMessage(content, &config)) {
    LOG(FATAL) << "Failed to parse config: " << config_path;
  }

  if (config.sync_pairs_size() == 0) {
    LOG(FATAL) << "No sync_pairs configured";
  }

  if (config.event_journal_path().empty()) {
    LOG(FATAL) << "event_journal_path not configured";
  }

  namespace fs = std::filesystem;
  std::error_code ec;

  fs::path journal_dir = fs::path(config.event_journal_path()).parent_path();
  if (!journal_dir.empty()) {
    fs::create_directories(journal_dir, ec);
    if (ec) {
      LOG(FATAL) << "Failed to create journal directory " << journal_dir
                 << ": " << ec.message();
    }
  }

  for (const auto& pair : config.sync_pairs()) {
    if (pair.source_dir().empty()) {
      LOG(FATAL) << "Empty source_dir in sync_pairs";
    }
    if (!fs::is_directory(pair.source_dir(), ec)) {
      LOG(FATAL) << "Source directory does not exist: " << pair.source_dir();
    }
    if (pair.dest_dirs_size() == 0) {
      LOG(FATAL) << "No dest_dirs for source: " << pair.source_dir();
    }
    for (const auto& dest : pair.dest_dirs()) {
      fs::create_directories(dest, ec);
      if (ec) {
        LOG(FATAL) << "Failed to create dest directory " << dest << ": "
                   << ec.message();
      }
    }
  }

  if (!config.log_dir().empty()) {
    fs::create_directories(config.log_dir(), ec);
    if (ec) {
      LOG(WARNING) << "Failed to create log directory " << config.log_dir()
                   << ": " << ec.message();
    } else {
      FLAGS_log_dir = config.log_dir();
    }
  }

  LOG(INFO) << "Syncer starting with " << config.sync_pairs_size()
            << " sync pair(s)";

  std::mutex journal_mutex;

  tbox::tools::syncer::FanotifyWatcher watcher(config, journal_mutex);
  if (!watcher.Init()) {
    LOG(FATAL) << "Failed to initialize fanotify watcher";
  }

  tbox::tools::syncer::SyncExecutor executor(config, journal_mutex);

  signal(SIGINT, SignalHandler);
  signal(SIGTERM, SignalHandler);

  watcher.Start();
  executor.Start();

  while (!g_shutdown.load()) {
    tbox::util::Util::Sleep(1000);
  }

  LOG(INFO) << "Shutting down syncer";
  executor.Stop();
  watcher.Stop();

  LOG(INFO) << "Syncer stopped";
  return 0;
}
