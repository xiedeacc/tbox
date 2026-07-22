/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/impl/user_manager.h"

namespace tbox {
namespace impl {

std::shared_ptr<UserManager> UserManager::Instance() {
  static std::shared_ptr<UserManager> instance(new UserManager());
  return instance;
}

}  // namespace impl
}  // namespace tbox
