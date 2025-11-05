/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_HTTP_HANDLER_SERVER_HANDLER_H
#define TBOX_SERVER_HTTP_HANDLER_SERVER_HANDLER_H

#include <sstream>

#include "proxygen/httpserver/RequestHandler.h"
#include "proxygen/httpserver/ResponseBuilder.h"
#include "proxygen/lib/http/HTTPMessage.h"
#include "src/server/handler/handler.h"
#include "src/server/http_handler/util.h"
#include "src/server/grpc_handler/report_handler.h"
#include "src/util/util.h"

namespace tbox {
namespace server {
namespace http_handler {

/**
 * @brief HTTP handler for server-related information endpoints.
 *
 * Returns the client IP address observed by the server and all registered
 * client IP addresses from the report handler. It prefers the X-Forwarded-For
 * or X-Real-IP headers (set by reverse proxies like Nginx), and falls back to
 * "unknown" if not present.
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
    // Get all registered clients from the report handler
    auto all_clients = grpc_handler::ReportOpHandler::GetAllClients();
    
    // Build comprehensive JSON response
    std::ostringstream json_stream;
    json_stream << "{";
    
    // Current client IP (from HTTP headers)
    json_stream << "\"current_client_ip\":\"";
    // Escape quotes if any (unlikely in IPs) to be safe
    for (char c : client_ip_) {
      if (c == '\\' || c == '"') {
        json_stream << '\\';
      }
      json_stream << c;
    }
    json_stream << "\",";
    
    // Total number of registered clients
    json_stream << "\"total_registered_clients\":" << all_clients.size() << ",";
    
    // All registered clients and their IP addresses
    json_stream << "\"registered_clients\":{";
    bool first_client = true;
    for (const auto& [client_id, client_info] : all_clients) {
      if (!first_client) {
        json_stream << ",";
      }
      first_client = false;
      
      json_stream << "\"" << client_id << "\":{";
      
      // IPv4 addresses
      json_stream << "\"ipv4\":[";
      for (size_t i = 0; i < client_info.ipv4_addresses.size(); ++i) {
        if (i > 0) json_stream << ",";
        json_stream << "\"" << client_info.ipv4_addresses[i] << "\"";
      }
      json_stream << "],";
      
      // IPv6 addresses
      json_stream << "\"ipv6\":[";
      for (size_t i = 0; i < client_info.ipv6_addresses.size(); ++i) {
        if (i > 0) json_stream << ",";
        json_stream << "\"" << client_info.ipv6_addresses[i] << "\"";
      }
      json_stream << "],";
      
      // Client info and timestamps (convert to human-readable format)
      json_stream << "\"client_info\":\"" << client_info.client_info << "\",";
      json_stream << "\"last_report_time\":\"" << util::Util::ToTimeStr(client_info.last_report_time_millis) << "\",";
      json_stream << "\"client_timestamp\":\"" << util::Util::ToTimeStr(client_info.client_timestamp * 1000) << "\"";
      
      json_stream << "}";
    }
    json_stream << "}";
    
    json_stream << "}";
    
    std::string res_body = json_stream.str();
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
