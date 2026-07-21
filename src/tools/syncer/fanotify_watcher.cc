/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 ******************************************************************************/

#include "src/tools/syncer/fanotify_watcher.h"

#include <fcntl.h>
#include <linux/limits.h>
#include <poll.h>
#include <sys/fanotify.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "glog/logging.h"
#include "src/util/util.h"

namespace tbox {
namespace tools {
namespace syncer {

namespace fs = std::filesystem;

static constexpr size_t kEventBufSize = 8192;

const char* EventTypeToString(EventType type) {
  switch (type) {
    case EventType::kCreate:
      return "CREATE";
    case EventType::kModify:
      return "MODIFY";
    case EventType::kDelete:
      return "DELETE";
    case EventType::kMovedFrom:
      return "MOVED_FROM";
    case EventType::kMovedTo:
      return "MOVED_TO";
  }
  return "UNKNOWN";
}

bool StringToEventType(const std::string& str, EventType* type) {
  if (str == "CREATE") {
    *type = EventType::kCreate;
  } else if (str == "MODIFY") {
    *type = EventType::kModify;
  } else if (str == "DELETE") {
    *type = EventType::kDelete;
  } else if (str == "MOVED_FROM") {
    *type = EventType::kMovedFrom;
  } else if (str == "MOVED_TO") {
    *type = EventType::kMovedTo;
  } else {
    return false;
  }
  return true;
}

FanotifyWatcher::FanotifyWatcher(const tbox::proto::SyncerConfig& config,
                                 std::mutex& journal_mutex)
    : config_(config), journal_mutex_(journal_mutex) {}

FanotifyWatcher::~FanotifyWatcher() { Stop(); }

bool FanotifyWatcher::Init() {
  fanotify_fd_ = fanotify_init(
      FAN_CLOEXEC | FAN_CLASS_NOTIF | FAN_REPORT_DIR_FID | FAN_REPORT_NAME,
      O_RDONLY | O_LARGEFILE);
  if (fanotify_fd_ < 0) {
    LOG(ERROR) << "fanotify_init failed: " << strerror(errno)
               << " (requires Linux 5.9+ and CAP_SYS_ADMIN)";
    return false;
  }

  for (const auto& pair : config_.sync_pairs()) {
    const std::string& src = pair.source_dir();
    if (!fs::is_directory(src)) {
      LOG(ERROR) << "Source directory does not exist: " << src;
      close(fanotify_fd_);
      fanotify_fd_ = -1;
      return false;
    }

    uint64_t mask = FAN_CREATE | FAN_MODIFY | FAN_DELETE | FAN_MOVED_FROM |
                    FAN_MOVED_TO | FAN_ONDIR | FAN_EVENT_ON_CHILD;
    if (fanotify_mark(fanotify_fd_, FAN_MARK_ADD, mask, AT_FDCWD,
                      src.c_str()) < 0) {
      LOG(ERROR) << "fanotify_mark failed for " << src << ": "
                 << strerror(errno);
      close(fanotify_fd_);
      fanotify_fd_ = -1;
      return false;
    }

    int mount_fd = open(src.c_str(), O_RDONLY | O_DIRECTORY);
    if (mount_fd < 0) {
      LOG(ERROR) << "Failed to open mount fd for " << src << ": "
                 << strerror(errno);
      close(fanotify_fd_);
      fanotify_fd_ = -1;
      return false;
    }
    mount_fds_[mount_fd] = src;

    LOG(INFO) << "Watching directory: " << src;
  }

  return true;
}

void FanotifyWatcher::Start() {
  running_.store(true);
  thread_ = std::thread(&FanotifyWatcher::EventLoop, this);
  LOG(INFO) << "FanotifyWatcher started";
}

void FanotifyWatcher::Stop() {
  running_.store(false);

  if (fanotify_fd_ >= 0) {
    close(fanotify_fd_);
    fanotify_fd_ = -1;
  }

  if (thread_.joinable()) {
    thread_.join();
  }

  for (auto& [fd, path] : mount_fds_) {
    close(fd);
  }
  mount_fds_.clear();

  LOG(INFO) << "FanotifyWatcher stopped";
}

void FanotifyWatcher::EventLoop() {
  alignas(struct fanotify_event_metadata) char buf[kEventBufSize];

  while (running_.load()) {
    struct pollfd pfd = {fanotify_fd_, POLLIN, 0};
    int ret = poll(&pfd, 1, 1000);
    if (ret < 0) {
      if (errno == EINTR) continue;
      LOG(ERROR) << "poll failed: " << strerror(errno);
      break;
    }
    if (ret == 0) continue;

    ssize_t len = read(fanotify_fd_, buf, sizeof(buf));
    if (len < 0) {
      if (errno == EAGAIN || errno == EINTR) continue;
      LOG(ERROR) << "read failed on fanotify fd: " << strerror(errno);
      break;
    }
    if (len > 0) {
      ProcessEvents(buf, len);
    }
  }
}

void FanotifyWatcher::ProcessEvents(const char* buf, ssize_t len) {
  const struct fanotify_event_metadata* metadata =
      reinterpret_cast<const struct fanotify_event_metadata*>(buf);

  while (FAN_EVENT_OK(metadata, len)) {
    if (metadata->vers != FANOTIFY_METADATA_VERSION) {
      LOG(ERROR) << "Mismatched fanotify metadata version";
      break;
    }

    if (metadata->fd != FAN_NOFD && metadata->fd >= 0) {
      close(metadata->fd);
    }

    if (metadata->mask & FAN_Q_OVERFLOW) {
      LOG(WARNING) << "Fanotify queue overflow, events may be lost";
      metadata = FAN_EVENT_NEXT(metadata, len);
      continue;
    }

    const struct fanotify_event_info_fid* fid = nullptr;
    const char* file_name = nullptr;

    const char* info_ptr =
        reinterpret_cast<const char*>(metadata) + metadata->event_len;
    const char* cur =
        reinterpret_cast<const char*>(metadata + 1);

    while (cur < info_ptr) {
      const struct fanotify_event_info_header* info_header =
          reinterpret_cast<const struct fanotify_event_info_header*>(cur);

      if (info_header->info_type == FAN_EVENT_INFO_TYPE_DFID_NAME) {
        fid = reinterpret_cast<const struct fanotify_event_info_fid*>(cur);
        const struct ::file_handle* fh =
            reinterpret_cast<const struct ::file_handle*>(fid->handle);
        file_name = reinterpret_cast<const char*>(fh->f_handle +
                                                   fh->handle_bytes);
        break;
      } else if (info_header->info_type == FAN_EVENT_INFO_TYPE_FID) {
        fid = reinterpret_cast<const struct fanotify_event_info_fid*>(cur);
        break;
      }

      cur += info_header->len;
    }

    if (fid == nullptr) {
      metadata = FAN_EVENT_NEXT(metadata, len);
      continue;
    }

    const struct ::file_handle* fh =
        reinterpret_cast<const struct ::file_handle*>(fid->handle);

    std::string dir_path;
    for (auto& [mount_fd, src_dir] : mount_fds_) {
      dir_path = ResolveFidPath(mount_fd, fh);
      if (!dir_path.empty()) break;
    }

    if (dir_path.empty()) {
      metadata = FAN_EVENT_NEXT(metadata, len);
      continue;
    }

    std::string full_path = dir_path;
    if (file_name != nullptr && file_name[0] != '\0') {
      if (full_path.back() != '/') full_path += '/';
      full_path += file_name;
    }

    EventType event_type = EventType::kModify;
    if (metadata->mask & FAN_CREATE) {
      event_type = EventType::kCreate;
    } else if (metadata->mask & FAN_DELETE) {
      event_type = EventType::kDelete;
    } else if (metadata->mask & FAN_MOVED_FROM) {
      event_type = EventType::kMovedFrom;
    } else if (metadata->mask & FAN_MOVED_TO) {
      event_type = EventType::kMovedTo;
    } else if (metadata->mask & FAN_MODIFY) {
      event_type = EventType::kModify;
    }

    int64_t ts = tbox::util::Util::CurrentTimeMillis();
    AppendToJournal(ts, event_type, full_path);

    LOG(INFO) << "Event: " << EventTypeToString(event_type) << " " << full_path;

    metadata = FAN_EVENT_NEXT(metadata, len);
  }
}

std::string FanotifyWatcher::ResolveFidPath(
    int mount_fd, const struct ::file_handle* fh) {
  int fd = open_by_handle_at(mount_fd, const_cast<struct ::file_handle*>(fh),
                             O_RDONLY | O_PATH);
  if (fd < 0) return "";

  char proc_path[64];
  snprintf(proc_path, sizeof(proc_path), "/proc/self/fd/%d", fd);

  char resolved[PATH_MAX];
  ssize_t rlen = readlink(proc_path, resolved, sizeof(resolved) - 1);
  close(fd);

  if (rlen < 0) return "";
  resolved[rlen] = '\0';
  return std::string(resolved);
}

void FanotifyWatcher::AppendToJournal(int64_t timestamp_ms, EventType type,
                                      const std::string& path) {
  std::lock_guard<std::mutex> lock(journal_mutex_);
  std::ofstream ofs(config_.event_journal_path(),
                    std::ios::app | std::ios::out);
  if (!ofs.is_open()) {
    LOG(ERROR) << "Failed to open journal file: "
               << config_.event_journal_path();
    return;
  }
  ofs << timestamp_ms << "|" << EventTypeToString(type) << "|" << path << "\n";
}

}  // namespace syncer
}  // namespace tools
}  // namespace tbox
