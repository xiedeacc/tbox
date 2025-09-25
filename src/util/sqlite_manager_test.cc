/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/util/sqlite_manager.h"

#include "gtest/gtest.h"

namespace tbox {
namespace util {

TEST(SqliteManager, ExecuteNonQuery) {
  EXPECT_EQ(SqliteManager::Instance()->Init(), true);
}

}  // namespace util
}  // namespace tbox
