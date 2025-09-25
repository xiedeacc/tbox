/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/server/handler/handler.h"

namespace tbox {
namespace server {
namespace handler {

absl::base_internal::SpinLock Handler::kLock;

}  // namespace handler
}  // namespace server
}  // namespace tbox
