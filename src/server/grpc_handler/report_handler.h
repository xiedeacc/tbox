/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_GRPC_HANDLER_REPORT_HANDLER_H_
#define TBOX_SERVER_GRPC_HANDLER_REPORT_HANDLER_H_

#include <chrono>
#include <ctime>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "glog/logging.h"
#include "src/async_grpc/rpc_handler.h"
#include "src/server/grpc_handler/meta.h"
#include "src/util/util.h"

namespace tbox {
namespace server {
namespace grpc_handler {

/// @brief Structure to store client IP information
struct ClientIPInfo {
  std::vector<std::string> ipv4_addresses;
  std::vector<std::string> ipv6_addresses;
  std::string client_info;
  int64_t last_report_time_millis = 0;  // Unix timestamp in milliseconds
  int64_t client_timestamp;
};

class ReportOpHandler : public async_grpc::RpcHandler<ReportOpMethod> {
 public:
  ReportOpHandler() = default;
  ~ReportOpHandler() = default;

  /// @brief Get a copy of all registered clients' information
  /// @return Map of client ID to ClientIPInfo
  static std::unordered_map<std::string, ClientIPInfo> GetAllClients() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return clients_map_;
  }

  /// @brief Get a specific client's information by ID
  /// @param client_id The client's token/ID
  /// @param info Output parameter for client info
  /// @return True if client was found, false otherwise
  static bool GetClientInfo(const std::string& client_id, ClientIPInfo& info) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    auto it = clients_map_.find(client_id);
    if (it != clients_map_.end()) {
      info = it->second;
      return true;
    }
    return false;
  }

  /// @brief Get the number of registered clients
  /// @return Number of clients currently tracked
  static size_t GetClientCount() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return clients_map_.size();
  }

  void OnRequest(const proto::ReportRequest& req) override {
    auto res = std::make_unique<proto::ReportResponse>();

    // Copy all client IPs to response
    for (const auto& ip : req.client_ip()) {
      res->add_client_ip(ip);
    }

    // Create a string representation of all IPs for logging
    std::ostringstream ip_stream;
    for (int i = 0; i < req.client_ip_size(); ++i) {
      if (i > 0)
        ip_stream << ", ";
      ip_stream << req.client_ip(i);
    }
    std::string all_ips = ip_stream.str();

    try {
      // Determine the operation based on the request op code
      switch (req.op()) {
        case proto::OpCode::OP_REPORT:
          HandleReport(req, res.get());
          break;
        default:
          res->set_err_code(proto::ErrCode::Fail);
          res->set_message("Invalid operation code for Report");
          LOG(ERROR) << "Invalid operation code: " << req.op();
          break;
      }
    } catch (const std::exception& e) {
      LOG(ERROR) << "Report operation failed: " << e.what();
      res->set_err_code(proto::ErrCode::Fail);
      res->set_message(std::string("Operation failed: ") + e.what());
    }

    Send(std::move(res));
  }

  void OnReadsDone() override { Finish(grpc::Status::OK); }

 private:
  bool IsIPv6Address(const std::string& ip) {
    // Simple check: IPv6 addresses contain colons
    return ip.find(':') != std::string::npos;
  }

  void HandleReport(const proto::ReportRequest& req,
                    proto::ReportResponse* res) {
    // Get current server time in CST (UTC+8)
    int64_t now_millis = util::Util::CurrentTimeMillis();
    std::time_t time_t_utc = now_millis / 1000;
    // Convert to CST by adding 8 hours
    auto time_t_cst = time_t_utc + (8 * 3600);

    std::stringstream server_time_ss;
    server_time_ss << std::put_time(std::gmtime(&time_t_cst),
                                    "%Y-%m-%d %H:%M:%S CST");
    std::string server_time_cst = server_time_ss.str();

    // Convert client timestamp to human-readable format (CST)
    std::time_t client_time_t = req.timestamp();
    auto client_time_cst = client_time_t + (8 * 3600);
    std::stringstream client_time_ss;
    client_time_ss << std::put_time(std::gmtime(&client_time_cst),
                                    "%Y-%m-%d %H:%M:%S CST");
    std::string client_time_str = client_time_ss.str();

    // Separate IPv4 and IPv6 addresses
    std::vector<std::string> ipv4_addrs;
    std::vector<std::string> ipv6_addrs;

    for (const auto& ip : req.client_ip()) {
      if (IsIPv6Address(ip)) {
        ipv6_addrs.push_back(ip);
      } else {
        ipv4_addrs.push_back(ip);
      }
    }

    // Store/update client information in the map
    std::string client_id = req.token();
    {
      std::lock_guard<std::mutex> lock(clients_mutex_);
      ClientIPInfo& client = clients_map_[client_id];
      client.ipv4_addresses = ipv4_addrs;
      client.ipv6_addresses = ipv6_addrs;
      client.client_info = req.client_info();
      client.last_report_time_millis = now_millis;
      client.client_timestamp = req.timestamp();
    }

    // Create JSON representation of IP addresses
    std::ostringstream json_stream;
    json_stream << "{";

    // IPv4 addresses
    json_stream << "\"ipv4\":[";
    for (size_t i = 0; i < ipv4_addrs.size(); ++i) {
      if (i > 0)
        json_stream << ",";
      json_stream << "\"" << ipv4_addrs[i] << "\"";
    }
    json_stream << "],";

    // IPv6 addresses
    json_stream << "\"ipv6\":[";
    for (size_t i = 0; i < ipv6_addrs.size(); ++i) {
      if (i > 0)
        json_stream << ",";
      json_stream << "\"" << ipv6_addrs[i] << "\"";
    }
    json_stream << "]";
    json_stream << "}";

    std::string ip_json = json_stream.str();

    // Create a string representation of all IPs for response message
    std::ostringstream ip_stream;
    for (int i = 0; i < req.client_ip_size(); ++i) {
      if (i > 0)
        ip_stream << ", ";
      ip_stream << req.client_ip(i);
    }
    std::string all_ips = ip_stream.str();

    // Get total number of registered clients
    size_t total_clients = 0;
    {
      std::lock_guard<std::mutex> lock(clients_mutex_);
      total_clients = clients_map_.size();
    }

    // Log the client IP report in JSON format
    LOG(INFO) << "============================================";
    LOG(INFO) << "  Client ID: " << client_id.substr(0, 16) << "...";
    LOG(INFO) << "  Total IPs: " << req.client_ip_size();
    LOG(INFO) << "  IP Addresses: " << ip_json;
    LOG(INFO) << "  Client Time: " << client_time_str;
    LOG(INFO) << "  Server Time: " << server_time_cst;
    LOG(INFO) << "  Client Info: " << req.client_info();
    LOG(INFO) << "  Total Registered Clients: " << total_clients;
    LOG(INFO) << "============================================";

    // Set response fields
    res->set_err_code(proto::ErrCode::Success);
    res->set_server_time(server_time_cst);

    std::ostringstream msg_stream;
    msg_stream << "Client IP report received successfully. "
               << req.client_ip_size() << " IP address(es) reported";
    if (!ipv6_addrs.empty()) {
      msg_stream << " (including " << ipv6_addrs.size() << " IPv6 address(es))";
    }
    msg_stream << ": [" << all_ips << "]";
    res->set_message(msg_stream.str());
  }

  // Static map to store all clients' IP information (keyed by token)
  static std::unordered_map<std::string, ClientIPInfo> clients_map_;
  static std::mutex clients_mutex_;
};

}  // namespace grpc_handler
}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_GRPC_HANDLER_REPORT_HANDLER_H_
