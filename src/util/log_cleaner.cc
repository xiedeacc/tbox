/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/util/log_cleaner.h"

namespace tbox {
namespace util {

static folly::Singleton<LogCleaner> log_cleaner;

std::shared_ptr<LogCleaner> LogCleaner::Instance() {
  return log_cleaner.try_get();
}

}  // namespace util
}  // namespace tbox
