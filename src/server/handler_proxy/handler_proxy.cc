/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/server/handler_proxy/handler_proxy.h"

namespace tbox {
namespace server {
namespace handler_proxy {

proto::Context HandlerProxy::kDevIPAddrs;
absl::base_internal::SpinLock HandlerProxy::kLock;

}  // namespace handler_proxy
}  // namespace server
}  // namespace tbox
