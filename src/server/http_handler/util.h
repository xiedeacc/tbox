/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_HTTP_HANDLER_UTIL_H
#define TBOX_SERVER_HTTP_HANDLER_UTIL_H

#include "proxygen/httpserver/ResponseBuilder.h"

namespace tbox {
namespace server {
namespace http_handler {

enum MultiPartField {
  MP_unknown = 0,
  MP_op,
  MP_path,
  MP_hash,
  MP_size,
  MP_content,
  MP_partition_num,
  MP_repo_uuid,
  MP_partition_size,
};

class Util {
 public:
  static void InternalServerError(const std::string& res_body,
                                  proxygen::ResponseHandler* downstream) {
    proxygen::ResponseBuilder(downstream)
        .status(500, "Internal Server Error")
        .body(res_body)
        .sendWithEOM();
  }

  static void Success(const std::string& res_body,
                      proxygen::ResponseHandler* downstream) {
    proxygen::ResponseBuilder(downstream)
        .status(200, "Ok")
        .body(res_body)
        .sendWithEOM();
  }
};

}  // namespace http_handler
}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_HTTP_HANDLER_UTIL_H
