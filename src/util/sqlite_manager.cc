/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/util/sqlite_manager.h"

namespace tbox {
namespace util {

static folly::Singleton<SqliteManager> sql_lite;

std::shared_ptr<SqliteManager> SqliteManager::Instance() {
  return sql_lite.try_get();
}

}  // namespace util
}  // namespace tbox
