/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/impl/session_manager.h"

namespace tbox {
namespace impl {

std::shared_ptr<SessionManager> SessionManager::Instance() {
  static std::shared_ptr<SessionManager> instance(new SessionManager());
  return instance;
}

}  // namespace impl
}  // namespace tbox
