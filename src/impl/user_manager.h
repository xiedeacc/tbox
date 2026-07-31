/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_IMPL_USER_MANAGER_H
#define TBOX_IMPL_USER_MANAGER_H

#include <atomic>
#include <cctype>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "sqlite3.h"
#include "src/common/ssh_key_auth.h"
#include "src/common/logging.h"
#include "src/common/error.h"
#include "src/impl/config_manager.h"
#include "src/impl/session_manager.h"
#include "src/impl/sqlite_manager.h"
#include "src/util/util.h"

namespace tbox {
namespace impl {

/**
 * @brief User account manager.
 *
 * Singleton class that manages user registration, login, logout, and password
 * management. Works with SqliteManager for persistence and SessionManager for
 * authentication tokens. Thread-safe singleton.
 */
class UserManager final {
 private:
  UserManager() {}

 public:
  /**
   * @brief Get singleton instance.
   * @return Shared pointer to UserManager instance.
   */
  static std::shared_ptr<UserManager> Instance();

  ~UserManager() {}

  /**
   * @brief Initialize user manager and database.
   *
   * Initializes SQLite database connection and creates preset user accounts.
   *
   * @return true if initialization successful, false otherwise.
   */
  bool Init() {
    if (!util::SqliteManager::Instance()->Init()) {
      return false;
    }
    return true;
  }

  /**
   * @brief Stop user manager.
   */
  void Stop() { stop_.store(true); }

  int32_t CreateLoginChallenge(const std::string& user,
                               const std::string& client_id,
                               const std::string& client_nonce,
                               std::string* challenge_id,
                               std::string* server_nonce,
                               std::string* server_signature,
                               int64_t* expires_at) {
    if (user.empty() || user.size() > 64 || client_id.empty() ||
        client_id.size() > 128 || client_nonce.size() != 32) {
      return Err_User_invalid_passwd;
    }

    std::string nonce(32, '\0');
    if (!util::Util::FillSecureRandomBytes(&nonce)) {
      return Err_User_login_error;
    }
    const std::string id = util::Util::UUID();
    const int64_t expiry = CurrentUnixSeconds() + kChallengeLifetimeSeconds;
    const std::string message = common::SshKeyAuth::BuildLoginMessage(
        id, client_nonce, nonce, user, client_id);

    auto config = util::ConfigManager::Instance();
    std::string signature;
    if (!common::SshKeyAuth::CreateServerProof(
            config->SshPrivateKeyPath(), config->SshPublicKeyPath(), message,
            &signature)) {
      LOG(ERROR) << "Failed to create login proof with the server SSH key";
      return Err_User_login_error;
    }
    if (!common::SshKeyAuth::VerifyServerProof(
            config->SshPrivateKeyPath(), config->SshPublicKeyPath(), message,
            signature)) {
      LOG(ERROR) << "Server could not verify its own login challenge";
      return Err_User_login_error;
    }
    LOG(INFO) << "Issued login challenge transcript "
              << util::Util::SHA256(message).substr(0, 16)
              << ", signature "
              << util::Util::SHA256(signature).substr(0, 16);

    {
      std::lock_guard<std::mutex> lock(challenges_mutex_);
      PruneChallengesLocked();
      if (challenges_.size() >= kMaxChallenges) {
        challenges_.erase(challenges_.begin());
      }
      challenges_[id] =
          LoginChallenge{user, client_id, client_nonce, nonce, expiry};
    }

    *challenge_id = id;
    *server_nonce = nonce;
    *server_signature = signature;
    *expires_at = expiry;
    return Err_Success;
  }

  int32_t UserLoginWithChallenge(const std::string& user,
                                 const std::string& password,
                                 const std::string& client_id,
                                 const std::string& challenge_id,
                                 const std::string& signature,
                                 std::string* token) {
    LoginChallenge challenge;
    {
      std::lock_guard<std::mutex> lock(challenges_mutex_);
      PruneChallengesLocked();
      const auto it = challenges_.find(challenge_id);
      if (it == challenges_.end()) {
        return Err_User_invalid_passwd;
      }
      challenge = it->second;
      challenges_.erase(it);
    }

    if (challenge.expires_at < CurrentUnixSeconds() ||
        challenge.user != user || challenge.client_id != client_id) {
      return Err_User_invalid_passwd;
    }

    const std::string message = common::SshKeyAuth::BuildLoginMessage(
        challenge_id, challenge.client_nonce, challenge.server_nonce, user,
        client_id);
    auto config = util::ConfigManager::Instance();
    if (!common::SshKeyAuth::VerifyClientProof(
            config->SshPrivateKeyPath(), config->SshPublicKeyPath(), message,
            signature)) {
      LOG(WARNING) << "Invalid SSH key login proof for client "
                   << client_id;
      return Err_User_invalid_passwd;
    }

    return UserLogin(user, password, token);
  }

  /**
   * @brief Register new user account.
   * @param user Username (1-64 characters).
   * @param password Password (1-64 characters).
   * @param token Output parameter for authentication token on success.
   * @return Err_Success on success, error code otherwise.
   */
  int32_t UserRegister(const std::string& user, const std::string& password,
                       std::string* token) {
    if (user.size() > 64 || user.empty()) {
      return Err_User_invalid_name;
    }

    if (!IsHex64(password)) {
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

  /**
   * @brief Delete user account.
   * @param login_user Currently logged in username.
   * @param to_delete_user Username to delete.
   * @param token Authentication token.
   * @return Err_Success on success, error code otherwise.
   */
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

  /**
   * @brief Authenticate user login.
   * @param user Username (1-64 characters).
   * @param password Password (1-64 characters).
   * @param token Output parameter for authentication token on success.
   * @return Err_Success on success, error code otherwise.
   */
  int32_t UserLogin(const std::string& user, const std::string& password,
                    std::string* token) {
    if (user.size() > 64 || user.empty()) {
      return Err_User_invalid_name;
    }

    if (!IsHex64(password)) {
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
      const unsigned char* salt_text = sqlite3_column_text(stmt, 0);
      const unsigned char* hash_text = sqlite3_column_text(stmt, 1);
      std::string salt =
          salt_text ? reinterpret_cast<const char*>(salt_text) : std::string();
      std::string stored_hash =
          hash_text ? reinterpret_cast<const char*>(hash_text) : std::string();
      sqlite3_finalize(stmt);
      if (util::Util::VerifyPassword(password, salt, stored_hash)) {
        *token = SessionManager::Instance()->GenerateToken(user);
        return Err_Success;
      } else {
        return Err_User_invalid_passwd;
      }
    }

    sqlite3_finalize(stmt);
    return Err_User_invalid_name;
  }

  /**
   * @brief Log out user session.
   * @param token Authentication token to invalidate.
   * @return Err_Success (always succeeds).
   */
  int32_t UserLogout(const std::string& token) {
    SessionManager::Instance()->KickoutByToken(token);
    return Err_Success;
  }

  /**
   * @brief Check if user exists in database.
   * @param user Username to check (1-64 characters).
   * @return Err_User_exists if user exists, Err_User_not_exists otherwise.
   */
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
      sqlite3_finalize(stmt);
      return Err_User_exists;
    }

    sqlite3_finalize(stmt);
    return Err_User_not_exists;
  }

  /**
   * @brief Change user password.
   * @param user Username (1-64 characters).
   * @param password New password (1-64 characters).
   * @param token Output parameter for new authentication token on success.
   * @return Err_Success on success, error code otherwise.
   */
  int32_t ChangePassword(const std::string& user, const std::string& password,
                         std::string* token) {
    if (user.size() > 64 || user.empty()) {
      return Err_User_invalid_name;
    }

    if (!IsHex64(password)) {
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
  struct LoginChallenge {
    std::string user;
    std::string client_id;
    std::string client_nonce;
    std::string server_nonce;
    int64_t expires_at = 0;
  };

  static int64_t CurrentUnixSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
  }

  void PruneChallengesLocked() {
    const int64_t now = CurrentUnixSeconds();
    for (auto it = challenges_.begin(); it != challenges_.end();) {
      if (it->second.expires_at < now) {
        it = challenges_.erase(it);
      } else {
        ++it;
      }
    }
  }

  /**
   * @brief Validate a SHA-256 hex digest (64 hex characters).
   */
  static bool IsHex64(const std::string& s) {
    if (s.size() != 64) {
      return false;
    }
    for (char c : s) {
      if (!std::isxdigit(static_cast<unsigned char>(c))) {
        return false;
      }
    }
    return true;
  }
  std::atomic<bool> stop_ = false;
  static constexpr int64_t kChallengeLifetimeSeconds = 60;
  static constexpr size_t kMaxChallenges = 4096;
  std::mutex challenges_mutex_;
  std::unordered_map<std::string, LoginChallenge> challenges_;
};

}  // namespace impl
}  // namespace tbox

#endif  // TBOX_IMPL_USER_MANAGER_H
