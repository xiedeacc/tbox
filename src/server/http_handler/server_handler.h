/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_HTTP_HANDLER_SERVER_HANDLER_H
#define TBOX_SERVER_HTTP_HANDLER_SERVER_HANDLER_H

#include <mutex>
#include <sstream>
#include <vector>

#include "aws/core/Aws.h"
#include "aws/core/client/ClientConfiguration.h"
#include "aws/ec2/EC2Client.h"
#include "aws/ec2/model/StartInstancesRequest.h"
#include "aws/ec2/model/StopInstancesRequest.h"
#include "folly/IPAddress.h"
#include "proxygen/httpserver/RequestHandler.h"
#include "proxygen/httpserver/ResponseBuilder.h"
#include "proxygen/lib/http/HTTPMessage.h"
#include "src/server/grpc_handler/report_handler.h"
#include "src/server/handler/handler.h"
#include "src/server/http_handler/util.h"
#include "src/server/version_info.h"
#include "src/util/util.h"

namespace tbox {
namespace server {
namespace http_handler {

/**
 * @brief HTTP handler for server-related information and EC2 management
 * endpoints.
 *
 * Returns the client IP address observed by the server, server IP address,
 * all registered client IP addresses from the report handler, and handles
 * EC2 instance management operations. It prefers the X-Forwarded-For
 * or X-Real-IP headers (set by reverse proxies like Nginx), and falls back to
 * "unknown" if not present.
 */
class ServerHandler : public proxygen::RequestHandler {
 public:
  ServerHandler() {
    // Initialize AWS SDK if not already initialized
    static std::once_flag init_flag;
    std::call_once(init_flag, []() {
      Aws::SDKOptions options;
      Aws::InitAPI(options);
    });
  }
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
    // Parse request body to check for EC2 operations
    std::string operation = ParseOperation(body_);

    if (operation == "ec2_start" || operation == "ec2_stop") {
      HandleEC2Operation(operation);
    } else {
      HandleServerInfo();
    }
  }
  void onUpgrade(proxygen::UpgradeProtocol protocol) noexcept override {}
  void requestComplete() noexcept override {}
  void onError(proxygen::ProxygenError err) noexcept override {}

 private:
  /// @brief Parse operation from request body
  std::string ParseOperation(const std::string& body) {
    // Simple JSON parsing to extract operation type
    if (body.find("\"operation\":\"ec2_start\"") != std::string::npos) {
      return "ec2_start";
    } else if (body.find("\"operation\":\"ec2_stop\"") != std::string::npos) {
      return "ec2_stop";
    }
    return "server_info";
  }

  /// @brief Get server's public IP addresses (IPv4 and IPv6)
  std::vector<std::string> GetServerIPAddresses() {
    std::vector<std::string> public_ips;

    // Try to get public IPv4 from AWS metadata service (for EC2 instances)
    std::string public_ipv4 = GetAWSPublicIPv4();
    if (!public_ipv4.empty()) {
      public_ips.push_back(public_ipv4);
    }

    // Get public IPv6 addresses using utility function
    std::vector<std::string> ipv6_addresses =
        util::Util::GetPublicIPv6Addresses();
    for (const auto& ipv6 : ipv6_addresses) {
      public_ips.push_back(ipv6);
    }

    // If AWS metadata didn't work for IPv4, fall back to external service
    // detection
    if (public_ipv4.empty()) {
      std::string external_ip = GetExternalPublicIP();
      if (!external_ip.empty()) {
        // Only add if it's not already in the list (avoid duplicates)
        bool already_exists = false;
        for (const auto& existing_ip : public_ips) {
          if (existing_ip == external_ip) {
            already_exists = true;
            break;
          }
        }
        if (!already_exists) {
          public_ips.push_back(external_ip);
        }
      }
    }

    return public_ips;
  }

  /// @brief Get public IPv4 address from AWS metadata service
  std::string GetAWSPublicIPv4() {
    // AWS metadata service endpoint for public IPv4
    std::string command =
        "curl -s --max-time 3 "
        "http://169.254.169.254/latest/meta-data/public-ipv4 2>/dev/null";
    std::string result = ExecuteCommand(command);

    // Remove any trailing whitespace
    result.erase(result.find_last_not_of(" \t\r\n") + 1);

    // Validate it's actually an IPv4 address
    try {
      if (!result.empty() && result.find("404") == std::string::npos) {
        folly::IPAddress ip_addr(result);
        if (ip_addr.isV4()) {
          return result;
        }
      }
    } catch (const std::exception& e) {
      // Invalid IP, ignore
    }
    return "";
  }

  /// @brief Get public IP address using external service (fallback)
  std::string GetExternalPublicIP() {
    // Try multiple external services for reliability
    std::vector<std::string> services = {
        "curl -s --max-time 5 https://checkip.amazonaws.com 2>/dev/null",
        "curl -s --max-time 5 https://ipinfo.io/ip 2>/dev/null",
        "curl -s --max-time 5 https://api.ipify.org 2>/dev/null"};

    for (const auto& command : services) {
      std::string result = ExecuteCommand(command);
      result.erase(result.find_last_not_of(" \t\r\n") + 1);

      // Validate it's a valid IP address
      try {
        if (!result.empty() &&
            result.size() < 50) {  // Reasonable IP length check
          folly::IPAddress ip_addr(result);
          return result;
        }
      } catch (const std::exception& e) {
        // Try next service
        continue;
      }
    }
    return "";
  }

  /// @brief Execute shell command and return output
  std::string ExecuteCommand(const std::string& command) {
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
      return "";
    }

    char buffer[128];
    std::string result = "";
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      result += buffer;
    }
    pclose(pipe);
    return result;
  }

  void HandleServerInfo() {
    // Get all registered clients from the report handler
    auto all_clients = grpc_handler::ReportOpHandler::GetAllClients();

    // Get server IP addresses
    std::vector<std::string> server_ips = GetServerIPAddresses();

    // Build comprehensive JSON response
    std::ostringstream json_stream;
    json_stream << "{";

    // Server IP addresses array
    json_stream << "\"server_ip\":[";
    for (size_t i = 0; i < server_ips.size(); ++i) {
      if (i > 0)
        json_stream << ",";
      json_stream << "\"" << server_ips[i] << "\"";
    }
    json_stream << "],";

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
        if (i > 0)
          json_stream << ",";
        json_stream << "\"" << client_info.ipv4_addresses[i] << "\"";
      }
      json_stream << "],";

      // IPv6 addresses
      json_stream << "\"ipv6\":[";
      for (size_t i = 0; i < client_info.ipv6_addresses.size(); ++i) {
        if (i > 0)
          json_stream << ",";
        json_stream << "\"" << client_info.ipv6_addresses[i] << "\"";
      }
      json_stream << "],";

      // Client info and timestamps (convert to human-readable format)
      json_stream << "\"client_info\":\"" << client_info.client_info << "\",";
      json_stream << "\"last_report_time\":\""
                  << util::Util::ToTimeStr(client_info.last_report_time_millis)
                  << "\",";
      json_stream << "\"client_timestamp\":\""
                  << util::Util::ToTimeStr(client_info.client_timestamp * 1000)
                  << "\"";

      json_stream << "}";
    }
    json_stream << "},";

    // Git commit information
    json_stream << "\"git_commit\":\"" << GIT_VERSION << "\"";

    json_stream << "}";

    std::string res_body = json_stream.str();
    Util::Success(res_body, downstream_);
  }

  void HandleEC2Operation(const std::string& operation) {
    // Extract instance_id and region from request body
    std::string instance_id = ExtractJSONValue(body_, "instance_id");
    std::string region = ExtractJSONValue(body_, "region");

    if (instance_id.empty()) {
      std::string error_response =
          "{\"error\":\"Instance ID is required for EC2 operations\"}";
      Util::InternalServerError(error_response, downstream_);
      return;
    }

    // Configure client with region if specified
    Aws::Client::ClientConfiguration config;
    if (!region.empty()) {
      config.region = region;
    }
    Aws::EC2::EC2Client ec2_client(config);

    // Get server IP addresses
    std::vector<std::string> server_ips = GetServerIPAddresses();

    std::ostringstream json_stream;
    json_stream << "{";
    
    // Server IP addresses array
    json_stream << "\"server_ip\":[";
    for (size_t i = 0; i < server_ips.size(); ++i) {
      if (i > 0)
        json_stream << ",";
      json_stream << "\"" << server_ips[i] << "\"";
    }
    json_stream << "],";
    
    json_stream << "\"current_client_ip\":\"" << client_ip_ << "\",";
    json_stream << "\"instance_id\":\"" << instance_id << "\",";

    if (operation == "ec2_start") {
      Aws::EC2::Model::StartInstancesRequest start_request;
      start_request.AddInstanceIds(instance_id);

      auto outcome = ec2_client.StartInstances(start_request);

      if (outcome.IsSuccess()) {
        json_stream << "\"status\":\"starting\",";
        json_stream
            << "\"message\":\"Instance start request submitted successfully\"";
        LOG(INFO) << "Successfully started instance: " << instance_id;
      } else {
        json_stream << "\"status\":\"error\",";
        json_stream << "\"message\":\"Failed to start instance: "
                    << outcome.GetError().GetMessage() << "\"";
        LOG(ERROR) << "Failed to start instance: " << instance_id << " - "
                   << outcome.GetError().GetMessage();
      }
    } else if (operation == "ec2_stop") {
      Aws::EC2::Model::StopInstancesRequest stop_request;
      stop_request.AddInstanceIds(instance_id);

      auto outcome = ec2_client.StopInstances(stop_request);

      if (outcome.IsSuccess()) {
        json_stream << "\"status\":\"stopping\",";
        json_stream
            << "\"message\":\"Instance stop request submitted successfully\"";
        LOG(INFO) << "Successfully stopped instance: " << instance_id;
      } else {
        json_stream << "\"status\":\"error\",";
        json_stream << "\"message\":\"Failed to stop instance: "
                    << outcome.GetError().GetMessage() << "\"";
        LOG(ERROR) << "Failed to stop instance: " << instance_id << " - "
                   << outcome.GetError().GetMessage();
      }
    }

    json_stream << "}";
    std::string res_body = json_stream.str();
    Util::Success(res_body, downstream_);
  }

  /// @brief Extract value from JSON string (simple implementation)
  std::string ExtractJSONValue(const std::string& json,
                               const std::string& key) {
    std::string search_key = "\"" + key + "\":\"";
    size_t start_pos = json.find(search_key);
    if (start_pos == std::string::npos) {
      return "";
    }
    start_pos += search_key.length();
    size_t end_pos = json.find("\"", start_pos);
    if (end_pos == std::string::npos) {
      return "";
    }
    return json.substr(start_pos, end_pos - start_pos);
  }

  std::string body_;
  std::string client_ip_;
};

}  // namespace http_handler
}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_HTTP_HANDLER_SERVER_HANDLER_H
