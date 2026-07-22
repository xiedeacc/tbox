/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/impl/sqlite_manager.h"

namespace tbox {
namespace util {

std::shared_ptr<SqliteManager> SqliteManager::Instance() {
  static std::shared_ptr<SqliteManager> instance(new SqliteManager());
  return instance;
}

}  // namespace util
}  // namespace tbox
