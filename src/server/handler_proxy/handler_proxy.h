/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_HANDLER_PROXY_H
#define TBOX_SERVER_HANDLER_PROXY_H

#include "src/impl/session_manager.h"
#include "src/impl/user_manager.h"
#include "src/proto/service.pb.h"
#include "src/util/util.h"

namespace tbox {
namespace server {
namespace handler_proxy {

class HandlerProxy {
 public:
  static void ServerOpHandle(const proto::ServerReq& req,
                             proto::ServerRes* res) {
    int32_t ret = Err_Success;
    switch (req.op()) {
      case proto::ServerOp::ServerUpdateIp:
        break;
      case proto::ServerOp::ServerUpdateDns:
        break;
      default:
        LOG(ERROR) << "Unsupported operation";
    }

    if (ret) {
      res->set_err_code(proto::ErrCode(ret));
    } else {
      res->set_err_code(proto::ErrCode::Success);
    }
  }
};

}  // namespace handler_proxy
}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_HANDLER_PROXY_H
