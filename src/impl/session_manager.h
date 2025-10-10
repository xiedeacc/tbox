/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_IMPL_SESSION_MANAGER_H
#define TBOX_IMPL_SESSION_MANAGER_H

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>

#include "absl/base/internal/spinlock.h"
#include "folly/Singleton.h"
#include "glog/logging.h"
#include "src/common/defs.h"
#include "src/util/util.h"

namespace tbox {
namespace impl {

/**
 * @brief Session data structure.
 * 
 * Holds user session information including username, token, and last update
 * timestamp.
 */
class Session final {
 public:
  std::string user;
  int64_t last_update_time;
  std::string token;
};

/**
 * @brief Session manager for user authentication tokens.
 * 
 * Singleton class that manages user sessions with token-based authentication.
 * Thread-safe implementation using spinlock. Sessions expire after
 * SESSION_INTERVAL milliseconds of inactivity.
 */
class SessionManager final {
 private:
  friend class folly::Singleton<SessionManager>;
  SessionManager() {}

 public:
  /**
   * @brief Get singleton instance.
   * @return Shared pointer to SessionManager instance.
   */
  static std::shared_ptr<SessionManager> Instance();

  ~SessionManager() {}

  /**
   * @brief Initialize session manager.
   * @return Always returns true.
   */
  bool Init() { return true; }

  /**
   * @brief Generate authentication token for user.
   * @param user Username to generate token for.
   * @return Generated authentication token (UUID).
   */
  std::string GenerateToken(const std::string& user) {
    auto token = util::Util::UUID();
    Session session;
    session.last_update_time = util::Util::CurrentTimeMillis();
    session.user = user;
    session.token = token;
    absl::base_internal::SpinLockHolder locker(&lock_);
    token_sessions_.emplace(token, session);
    user_sessions_.emplace(user, session);
    return token;
  }

  /**
   * @brief Validate session token and refresh timestamp.
   * @param token Authentication token to validate.
   * @param user Output parameter for username if validation succeeds.
   * @return true if token is valid and not expired, false otherwise.
   */
  bool ValidateSession(const std::string& token, std::string* user) {
    if (token.empty()) {
      return false;
    }
    absl::base_internal::SpinLockHolder locker(&lock_);
    auto token_it = token_sessions_.find(token);
    if (token_it == token_sessions_.end()) {
      return false;
    }
    auto now = util::Util::CurrentTimeMillis();
    if (now - token_it->second.last_update_time >= common::SESSION_INTERVAL) {
      return false;
    }

    // Update timestamp in both maps to maintain consistency
    token_it->second.last_update_time = now;
    auto user_it = user_sessions_.find(token_it->second.user);
    if (user_it != user_sessions_.end()) {
      user_it->second.last_update_time = now;
    }
    *user = token_it->second.user;
    return true;
  }

  /**
   * @brief Remove session by username.
   * @param user Username whose session should be removed.
   */
  void KickoutByUser(const std::string& user) {
    absl::base_internal::SpinLockHolder locker(&lock_);
    auto user_it = user_sessions_.find(user);
    if (user_it == user_sessions_.end()) {
      return;
    }

    auto token_it = token_sessions_.find(user_it->second.token);
    if (token_it == token_sessions_.end()) {
      LOG(ERROR) << "Cannot find user's token, this should never happen";
      return;
    }
    user_sessions_.erase(user_it);
    token_sessions_.erase(token_it);
  }

  /**
   * @brief Remove session by token.
   * @param token Authentication token whose session should be removed.
   */
  void KickoutByToken(const std::string& token) {
    absl::base_internal::SpinLockHolder locker(&lock_);
    auto token_it = token_sessions_.find(token);
    if (token_it == token_sessions_.end()) {
      return;
    }

    auto user_it = user_sessions_.find(token_it->second.user);
    if (user_it == user_sessions_.end()) {
      LOG(ERROR) << "Cannot find token's user, this should never happen";
      return;
    }

    user_sessions_.erase(user_it);
    token_sessions_.erase(token_it);
  }

  /**
   * @brief Stop session manager.
   */
  void Stop() { stop_.store(true); }

 private:
  mutable absl::base_internal::SpinLock lock_;
  std::atomic<bool> stop_ = false;
  std::unordered_map<std::string, Session> token_sessions_;
  std::unordered_map<std::string, Session> user_sessions_;
};

}  // namespace impl
}  // namespace tbox

#endif  // TBOX_IMPL_SESSION_MANAGER_H
