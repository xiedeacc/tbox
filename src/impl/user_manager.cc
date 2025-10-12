/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/impl/user_manager.h"

namespace tbox {
namespace impl {

static folly::Singleton<UserManager> user_manager;

std::shared_ptr<UserManager> UserManager::Instance() {
  return user_manager.try_get();
}

}  // namespace impl
}  // namespace tbox
