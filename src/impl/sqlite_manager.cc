/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/impl/sqlite_manager.h"

namespace tbox {
namespace util {

static folly::Singleton<SqliteManager> sqlite_manager;

std::shared_ptr<SqliteManager> SqliteManager::Instance() {
  return sqlite_manager.try_get();
}

}  // namespace util
}  // namespace tbox
