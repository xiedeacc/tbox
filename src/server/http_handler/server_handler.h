/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_HTTP_HANDLER_SERVER_HANDLER_H
#define TBOX_SERVER_HTTP_HANDLER_SERVER_HANDLER_H

#include "proxygen/httpserver/RequestHandler.h"
#include "proxygen/httpserver/ResponseBuilder.h"
#include "proxygen/lib/http/HTTPMessage.h"
#include "src/server/handler/handler.h"
#include "src/server/http_handler/util.h"

namespace tbox {
namespace server {
namespace http_handler {

/**
 * @brief HTTP handler for server-related information endpoints.
 *
 * Currently returns the client IP address observed by the server. It prefers
 * the X-Forwarded-For or X-Real-IP headers (set by reverse proxies like Nginx),
 * and falls back to "unknown" if not present.
 */
class ServerHandler : public proxygen::RequestHandler {
 public:
  /**
   * @brief Capture request headers to extract client IP.
   */
  void onRequest(
      std::unique_ptr<proxygen::HTTPMessage> headers) noexcept override {
    const proxygen::HTTPHeaders& http_headers = headers->getHeaders();
    // Prefer X-Forwarded-For, then X-Real-IP
    folly::StringPiece xff = http_headers.getSingleOrEmpty("x-forwarded-for");
    folly::StringPiece xri = http_headers.getSingleOrEmpty("x-real-ip");

    if (!xff.empty()) {
      // In case of multiple IPs, take the first
      std::string xff_str = xff.str();
      auto comma_pos = xff_str.find(',');
      client_ip_ = (comma_pos == std::string::npos)
                       ? xff_str
                       : xff_str.substr(0, comma_pos);
    } else if (!xri.empty()) {
      client_ip_ = xri.str();
    } else {
      client_ip_ = "unknown";
    }
  }
  void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override {
    body_.append(reinterpret_cast<const char*>(body->data()), body->length());
  }
  void onEOM() noexcept override {
    // Minimal JSON response with client IP
    std::string res_body;
    res_body.reserve(64);
    res_body.append("{\"client_ip\":\"");
    // Escape quotes if any (unlikely in IPs) to be safe
    for (char c : client_ip_) {
      if (c == '\\' || c == '"') {
        res_body.push_back('\\');
      }
      res_body.push_back(c);
    }
    res_body.append("\"}");
    Util::Success(res_body, downstream_);
  }
  void onUpgrade(proxygen::UpgradeProtocol protocol) noexcept override {}
  void requestComplete() noexcept override {}
  void onError(proxygen::ProxygenError err) noexcept override {}

 private:
  std::string body_;
  std::string client_ip_;
};

}  // namespace http_handler
}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_HTTP_HANDLER_SERVER_HANDLER_H
