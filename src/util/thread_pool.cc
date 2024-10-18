/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/util/thread_pool.h"

namespace tbox {
namespace util {

static folly::Singleton<ThreadPool> thread_pool;

std::shared_ptr<ThreadPool> ThreadPool::Instance() {
  return thread_pool.try_get();
}

}  // namespace util
}  // namespace tbox
