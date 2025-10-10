/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/impl/config_manager.h"

#include "glog/logging.h"
#include "gtest/gtest.h"

namespace tbox {
namespace util {

TEST(ConfigManager, Init) {
  EXPECT_TRUE(ConfigManager::Instance()->Init("./conf/server_config.json"));
  LOG(INFO) << ConfigManager::Instance()->ToString();
}

}  // namespace util
}  // namespace tbox
