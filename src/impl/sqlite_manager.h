/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_UTIL_SQLITE_MANAGER_H
#define TBOX_UTIL_SQLITE_MANAGER_H

#include <memory>
#include <string>

#include "external/sqlite/sqlite3.h"
#include "folly/Singleton.h"
#include "src/common/error.h"
#include "src/util/util.h"

namespace tbox {
namespace util {

/**
 * @brief SQLite database manager.
 * 
 * Singleton class that manages SQLite database connections and operations.
 * Handles user database initialization and provides prepared statement
 * and query execution interfaces. Thread-safe singleton using folly::Singleton.
 */
class SqliteManager final {
 private:
  friend class folly::Singleton<SqliteManager>;
  SqliteManager() {}

 public:
  /**
   * @brief Get singleton instance.
   * @return Shared pointer to SqliteManager instance.
   */
  static std::shared_ptr<SqliteManager> Instance();

  ~SqliteManager() {
    if (db_) {
      sqlite3_close(db_);
    }
  }

  /**
   * @brief Initialize database connection and schema.
   * 
   * Opens database at ~/data/user.db, creates users table if needed,
   * and initializes default admin user.
   * 
   * @return true if initialization successful, false otherwise.
   */
  bool Init() {
    std::string home_dir = tbox::util::Util::HomeDir();
    std::string user_db_path = home_dir + "/data/user.db";
    if (sqlite3_open(user_db_path.c_str(), &db_) != SQLITE_OK) {
      LOG(ERROR) << "open database error: " << user_db_path;
      return false;
    }

    std::string error_msg;
    if (ExecuteNonQuery("CREATE TABLE IF NOT EXISTS users ("
                        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                        "user TEXT UNIQUE, "
                        "salt TEXT, "
                        "password TEXT);",
                        &error_msg)) {
      LOG(ERROR) << "Init database error";
      return false;
    }

    sqlite3_stmt* stmt = nullptr;
    auto ret = util::SqliteManager::Instance()->PrepareStatement(
        "INSERT OR IGNORE INTO users (user, salt, password) VALUES (?, ?, ?);",
        &stmt);
    if (ret) {
      LOG(ERROR) << "Init admin prepare error";
      return false;
    }

    std::vector<uint8_t> salt_arr{0x45, 0x2c, 0x03, 0x06, 0x73, 0x0b,
                                  0x0f, 0x3a, 0xc3, 0x08, 0x6d, 0x4f,
                                  0x62, 0xef, 0xfc, 0x20};
    std::vector<uint8_t> hashed_password_arr{
        0x29, 0x9a, 0xe5, 0x3a, 0xb2, 0x2c, 0x08, 0x5a, 0x47, 0x96, 0xb5,
        0x91, 0x87, 0xd2, 0xb5, 0x4c, 0x21, 0x7e, 0x48, 0x30, 0xb4, 0xab,
        0xe4, 0xad, 0xe7, 0x9d, 0x7d, 0x8e, 0x6d, 0x90, 0xf5, 0x1a};
    std::string salt(salt_arr.begin(), salt_arr.end());
    std::string hashed_password(hashed_password_arr.begin(),
                                hashed_password_arr.end());
    sqlite3_bind_text(stmt, 1, "admin", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, salt.c_str(), Util::kSaltSize, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, hashed_password.c_str(), Util::kDerivedKeySize,
                      SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
      LOG(ERROR) << "Init admin execute error: " << sqlite3_errmsg(db_);
      LOG(ERROR) << sqlite3_errmsg(db_);
      sqlite3_finalize(stmt);
      return false;
    }
    if (AffectRows() > 0) {
      LOG(INFO) << "Init admin success";
    } else {
      LOG(INFO) << "Already exists admin";
    }

    sqlite3_finalize(stmt);
    return true;
  }

  /**
   * @brief Prepare SQL statement.
   * @param query SQL query string.
   * @param stmt Output parameter for prepared statement.
   * @return Err_Success on success, Err_Sql_prepare_error on failure.
   */
  int32_t PrepareStatement(const std::string& query, sqlite3_stmt** stmt) {
    if (sqlite3_prepare_v2(db_, query.c_str(), -1, stmt, nullptr) !=
        SQLITE_OK) {
      LOG(ERROR) << sqlite3_errmsg(db_);
      return Err_Sql_prepare_error;
    }
    return Err_Success;
  }

  /**
   * @brief Execute non-query SQL statement.
   * @param query SQL query string.
   * @param error_msg Output parameter for error message if execution fails.
   * @return Err_Success on success, Err_Sql_execute_error on failure.
   */
  int32_t ExecuteNonQuery(const std::string& query, std::string* error_msg) {
    char* errmsg = nullptr;
    if (sqlite3_exec(db_, query.c_str(), nullptr, nullptr, &errmsg) !=
        SQLITE_OK) {
      error_msg->append(errmsg);
      sqlite3_free(errmsg);
      return Err_Sql_execute_error;
    }
    return Err_Success;
  }

  /**
   * @brief Get number of rows affected by last operation.
   * @return Number of rows changed.
   */
  int32_t AffectRows() const { return sqlite3_changes(db_); }

 private:
  sqlite3* db_ = nullptr;
};

}  // namespace util
}  // namespace tbox

#endif  // TBOX_UTIL_SQLITE_MANAGER_H
