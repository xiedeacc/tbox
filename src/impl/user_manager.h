/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_IMPL_USER_MANAGER_H
#define TBOX_IMPL_USER_MANAGER_H

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

#include "absl/base/internal/spinlock.h"
#include "folly/Singleton.h"
#include "src/impl/session_manager.h"
#include "src/util/sqlite_manager.h"
#include "src/util/util.h"

namespace tbox {
namespace impl {

class UserManager final {
 private:
  friend class folly::Singleton<UserManager>;
  UserManager() {}

 public:
  static std::shared_ptr<UserManager> Instance();

  ~UserManager() {}

  bool Init() {
    if (!util::SqliteManager::Instance()->Init()) {
      return false;
    }
    
    // Initialize preset user: tiger with password qh6288QHW
    InitializePresetUser();
    
    return true;
  }

  void Stop() {
    stop_.store(true);
    cv_.notify_all();
  }

  int32_t UserRegister(const std::string& user, const std::string& password,
                       std::string* token) {
    if (user.size() > 64 || user.empty()) {
      return Err_User_invalid_name;
    }

    if (password.size() > 64 || password.empty()) {
      return Err_User_invalid_passwd;
    }

    std::string salt = util::Util::GenerateSalt();
    std::string hashed_password;
    if (!util::Util::HashPassword(password, salt, &hashed_password)) {
      return Err_Fail;
    }

    sqlite3_stmt* stmt = nullptr;
    auto ret = util::SqliteManager::Instance()->PrepareStatement(
        "INSERT OR IGNORE INTO users (user, salt, password) VALUES (?, ?, ?);",
        &stmt);
    if (ret) {
      return Err_User_register_prepare_error;
    }

    sqlite3_bind_text(stmt, 1, user.c_str(), user.size(), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, salt.c_str(), salt.size(), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, hashed_password.c_str(), hashed_password.size(),
                      SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
      sqlite3_finalize(stmt);
      return Err_User_register_execute_error;
    }
    sqlite3_finalize(stmt);

    int changes = util::SqliteManager::Instance()->AffectRows();
    if (changes > 0) {
      *token = SessionManager::Instance()->GenerateToken(user);
      return Err_Success;
    } else {
      LOG(ERROR) << "No records were updated. user '" << user
                 << " may exist already";
    }
    return Err_User_exists;
  }

  int32_t UserDelete(const std::string& login_user,
                     const std::string& to_delete_user,
                     const std::string& token) {
    if (to_delete_user.size() > 64 || to_delete_user.empty()) {
      return Err_User_invalid_name;
    }

    if (to_delete_user == "admin") {
      LOG(ERROR) << "Cannot delete admin";
      return Err_User_invalid_name;
    }

    if (login_user == "admin" || login_user == to_delete_user) {
      sqlite3_stmt* stmt = nullptr;
      auto ret = util::SqliteManager::Instance()->PrepareStatement(
          "DELETE FROM users WHERE user = ?;", &stmt);
      if (ret) {
        return Err_User_delete_prepare_error;
      }
      sqlite3_bind_text(stmt, 1, to_delete_user.c_str(), to_delete_user.size(),
                        SQLITE_STATIC);
      if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return Err_User_delete_execute_error;
      }
      sqlite3_finalize(stmt);
      int changes = util::SqliteManager::Instance()->AffectRows();
      if (changes > 0) {
        SessionManager::Instance()->KickoutByToken(token);
        return Err_Success;
      } else {
        LOG(ERROR) << "No records were deleted. user '" << to_delete_user
                   << " may not exist";
      }
    }
    return Err_User_invalid_name;
  }

  int32_t UserLogin(const std::string& user, const std::string& password,
                    std::string* token) {
    if (user.size() > 64 || user.empty()) {
      return Err_User_invalid_name;
    }

    if (password.size() > 64 || password.empty()) {
      return Err_User_invalid_passwd;
    }

    sqlite3_stmt* stmt = nullptr;
    util::SqliteManager::Instance()->PrepareStatement(
        "SELECT salt, password FROM users WHERE user = ?;", &stmt);
    if (!stmt) {
      return Err_User_login_prepare_error;
    }

    sqlite3_bind_text(stmt, 1, user.c_str(), user.size(), SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
      std::string salt;
      salt.reserve(util::Util::kSaltSize);
      salt.append(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)),
                  util::Util::kSaltSize);
      std::string hashed_password;
      hashed_password.reserve(util::Util::kDerivedKeySize);
      hashed_password.append(
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)),
          util::Util::kDerivedKeySize);
      sqlite3_finalize(stmt);
      if (util::Util::VerifyPassword(password, salt, hashed_password)) {
        *token = SessionManager::Instance()->GenerateToken(user);
        return Err_Success;
      } else {
        return Err_User_invalid_passwd;
      }
    }

    sqlite3_finalize(stmt);
    return Err_User_invalid_name;
  }

  int32_t UserLogout(const std::string& token) {
    SessionManager::Instance()->KickoutByToken(token);
    return Err_Success;
  }

  int32_t UserExists(const std::string& user) {
    if (user.size() > 64 || user.empty()) {
      return Err_User_invalid_name;
    }

    sqlite3_stmt* stmt = nullptr;
    util::SqliteManager::Instance()->PrepareStatement(
        "SELECT salt, password FROM users WHERE user = ?;", &stmt);
    if (!stmt) {
      return Err_User_exists_prepare_error;
    }

    sqlite3_bind_text(stmt, 1, user.c_str(), user.size(), SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
      std::string hashed_password;
      hashed_password.reserve(util::Util::kDerivedKeySize);
      hashed_password.append(
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)),
          util::Util::kDerivedKeySize);
      sqlite3_finalize(stmt);
      return Err_User_exists;
    }

    sqlite3_finalize(stmt);
    return Err_User_not_exists;
  }

  int32_t ChangePassword(const std::string& user, const std::string& password,
                         std::string* token) {
    if (user.size() > 64 || user.empty()) {
      return Err_User_invalid_name;
    }

    if (password.size() > 64 || password.empty()) {
      return Err_User_invalid_passwd;
    }

    std::string salt = util::Util::GenerateSalt();
    std::string hashed_password;
    if (!util::Util::HashPassword(password, salt, &hashed_password)) {
      return Err_User_change_password_error;
    }

    sqlite3_stmt* stmt = nullptr;
    auto ret = util::SqliteManager::Instance()->PrepareStatement(
        "UPDATE users SET salt = ?, password = ? WHERE user = ?;", &stmt);
    if (ret) {
      return Err_User_change_password_error;
    }

    sqlite3_bind_text(stmt, 3, user.c_str(), user.size(), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 1, salt.c_str(), salt.size(), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, hashed_password.c_str(), hashed_password.size(),
                      SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
      sqlite3_finalize(stmt);
      return Err_User_change_password_error;
    }
    sqlite3_finalize(stmt);

    int changes = util::SqliteManager::Instance()->AffectRows();
    if (changes > 0) {
      *token = SessionManager::Instance()->GenerateToken(user);
      return Err_Success;
    } else {
      LOG(ERROR) << "No records were updated. user '" << user
                 << " may not exist.";
    }
    return Err_User_change_password_error;
  }

 private:
  void InitializePresetUser() {
    const std::string preset_user = "tiger";
    const std::string preset_password = "qh6288QHW";
    
    // Check if user already exists
    int32_t exists_result = UserExists(preset_user);
    if (exists_result == Err_User_exists) {
      LOG(INFO) << "Preset user '" << preset_user << "' already exists";
      return;
    }
    
    // Register the preset user
    std::string token;
    int32_t result = UserRegister(preset_user, preset_password, &token);
    
    if (result == Err_Success) {
      LOG(INFO) << "Successfully initialized preset user '" << preset_user << "'";
    } else if (result == Err_User_exists) {
      LOG(INFO) << "Preset user '" << preset_user << "' already exists (concurrent registration)";
    } else {
      LOG(ERROR) << "Failed to initialize preset user '" << preset_user 
                 << "', error code: " << result;
    }
  }

  mutable absl::base_internal::SpinLock lock_;
  std::atomic<bool> stop_ = false;
  std::mutex mu_;
  std::condition_variable cv_;
};

}  // namespace impl
}  // namespace tbox

#endif  // TBOX_IMPL_USER_MANAGER_H
