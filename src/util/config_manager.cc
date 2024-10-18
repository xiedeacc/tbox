/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/util/config_manager.h"

namespace tbox {
namespace util {

static folly::Singleton<ConfigManager> config_manager;

std::shared_ptr<ConfigManager> ConfigManager::Instance() {
  return config_manager.try_get();
}

}  // namespace util
}  // namespace tbox
