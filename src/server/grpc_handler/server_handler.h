/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_GRPC_HANDLER_SERVER_HANDLER_H_
#define TBOX_SERVER_GRPC_HANDLER_SERVER_HANDLER_H_

#include <memory>
#include <string>

#include "aws/core/Aws.h"
#include "aws/core/client/ClientConfiguration.h"
#include "aws/ec2/EC2Client.h"
#include "aws/ec2/model/StartInstancesRequest.h"
#include "aws/ec2/model/StopInstancesRequest.h"
#include "glog/logging.h"
#include "src/async_grpc/rpc_handler.h"
#include "src/server/grpc_handler/meta.h"
#include "src/server/grpc_handler/report_handler.h"
#include "src/util/util.h"

namespace tbox {
namespace server {
namespace grpc_handler {

class ServerOpHandler : public async_grpc::RpcHandler<ServerOpMethod> {
 public:
  ServerOpHandler() {
    // Initialize AWS SDK
    Aws::SDKOptions options;
    Aws::InitAPI(options);
    aws_initialized_ = true;
  }

  ~ServerOpHandler() {
    // Cleanup AWS SDK if we initialized it
    if (aws_initialized_) {
      Aws::SDKOptions options;
      Aws::ShutdownAPI(options);
    }
  }

  void OnRequest(const proto::ServerRequest& req) override {
    LOG(INFO) << "ServerOpHandler::OnRequest received - request_id: "
              << req.request_id() << ", op: " << req.op();
    auto res = std::make_unique<proto::ServerResponse>();

    // Get server IP address
    std::string server_ip = GetServerIPAddress();
    res->set_server_ip(server_ip);

    // Get real client IP from gRPC metadata (nginx headers)
    std::string client_ip = GetRealClientIP();
    res->set_current_client_ip(client_ip);

    try {
      switch (req.op()) {
        case proto::OpCode::OP_SERVER_INFO:
          HandleServerInfo(req, res.get());
          break;
        case proto::OpCode::OP_EC2_START:
          HandleStartInstance(req, res.get());
          break;
        case proto::OpCode::OP_EC2_STOP:
          HandleStopInstance(req, res.get());
          break;
        default:
          res->set_err_code(proto::ErrCode::Fail);
          res->set_message("Invalid operation code for server operations");
          LOG(ERROR) << "Invalid operation code: " << req.op();
          break;
      }
    } catch (const std::exception& e) {
      LOG(ERROR) << "Server operation failed: " << e.what();
      res->set_err_code(proto::ErrCode::Fail);
      res->set_message(std::string("Operation failed: ") + e.what());
    }

    Send(std::move(res));
  }

  void OnReadsDone() override { Finish(grpc::Status::OK); }

 private:
  /// @brief Get real client IP from gRPC metadata (nginx headers)
  /// @return Client IP address from X-Real-IP or X-Forwarded-For header
  std::string GetRealClientIP() {
    auto* server_context = GetRpc()->server_context();
    const auto& metadata = server_context->client_metadata();

    // Try X-Real-IP first (set by nginx)
    auto real_ip_iter = metadata.find("x-real-ip");
    if (real_ip_iter != metadata.end()) {
      std::string ip(real_ip_iter->second.data(), real_ip_iter->second.size());
      return ip;
    }

    // Fallback to X-Forwarded-For (first IP in the list)
    auto forwarded_iter = metadata.find("x-forwarded-for");
    if (forwarded_iter != metadata.end()) {
      std::string forwarded(forwarded_iter->second.data(),
                            forwarded_iter->second.size());
      // Extract first IP from comma-separated list
      size_t comma_pos = forwarded.find(',');
      std::string ip = (comma_pos != std::string::npos)
                           ? forwarded.substr(0, comma_pos)
                           : forwarded;
      // Trim whitespace
      ip.erase(0, ip.find_first_not_of(" \t"));
      ip.erase(ip.find_last_not_of(" \t") + 1);
      return ip;
    }

    // Fallback to peer() if headers not available
    std::string peer_addr = server_context->peer();
    std::string ip = ExtractIPFromPeer(peer_addr);
    LOG(WARNING) << "No X-Real-IP or X-Forwarded-For header, using peer: "
                 << ip;
    return ip;
  }

  /// @brief Extract IP address from gRPC peer string
  /// @param peer Peer string in format "ipv4:x.x.x.x:port" or
  /// "ipv6:[addr]:port"
  /// @return Extracted IP address
  std::string ExtractIPFromPeer(const std::string& peer) {
    // Format examples:
    // "ipv4:192.168.1.1:12345"
    // "ipv6:[2001:db8::1]:12345"

    size_t colon_pos = peer.find(':');
    if (colon_pos == std::string::npos) {
      return "";
    }

    std::string protocol = peer.substr(0, colon_pos);
    std::string remainder = peer.substr(colon_pos + 1);

    if (protocol == "ipv4") {
      // For IPv4: extract everything between first : and last :
      size_t last_colon = remainder.rfind(':');
      if (last_colon != std::string::npos) {
        return remainder.substr(0, last_colon);
      }
    } else if (protocol == "ipv6") {
      // For IPv6: extract everything between [ and ]
      size_t bracket_start = remainder.find('[');
      size_t bracket_end = remainder.find(']');
      if (bracket_start != std::string::npos &&
          bracket_end != std::string::npos) {
        return remainder.substr(bracket_start + 1,
                                bracket_end - bracket_start - 1);
      }
    }

    return "";
  }

  /// @brief Get server's IP address
  /// @return Server IP address
  std::string GetServerIPAddress() {
    // Try to get server IP using hostname
    std::string command = "hostname -I | awk '{print $1}'";
    std::string result = ExecuteCommand(command);
    
    // Remove any trailing whitespace
    result.erase(result.find_last_not_of(" \t\r\n") + 1);
    
    if (result.empty()) {
      // Fallback method - get IP from network interface
      command = "ip route get 8.8.8.8 | awk '{print $7; exit}'";
      result = ExecuteCommand(command);
      result.erase(result.find_last_not_of(" \t\r\n") + 1);
    }
    
    return result.empty() ? "unknown" : result;
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

  void HandleServerInfo(const proto::ServerRequest& req,
                        proto::ServerResponse* res) {
    // Get all registered clients from the report handler
    auto all_clients = ReportOpHandler::GetAllClients();

    res->set_total_registered_clients(static_cast<int32_t>(all_clients.size()));

    // Add all registered clients to the response
    for (const auto& [client_id, client_info] : all_clients) {
      proto::ClientInfo* pb_client = res->add_registered_clients();
      pb_client->set_client_id(client_id);

      // Add IPv4 addresses
      for (const auto& ipv4 : client_info.ipv4_addresses) {
        pb_client->add_ipv4_addresses(ipv4);
      }

      // Add IPv6 addresses
      for (const auto& ipv6 : client_info.ipv6_addresses) {
        pb_client->add_ipv6_addresses(ipv6);
      }

      pb_client->set_client_info(client_info.client_info);
      pb_client->set_last_report_time(
          util::Util::ToTimeStr(client_info.last_report_time_millis));
      pb_client->set_client_timestamp(
          util::Util::ToTimeStr(client_info.client_timestamp * 1000));
    }

    res->set_err_code(proto::ErrCode::Success);
    res->set_message("Server information retrieved successfully");
    LOG(INFO) << "Server info request completed. Total clients: "
              << all_clients.size();
  }

  void HandleStartInstance(const proto::ServerRequest& req,
                           proto::ServerResponse* res) {
    if (req.instance_id().empty()) {
      res->set_err_code(proto::ErrCode::Fail);
      res->set_message("Instance ID is required for EC2 operations");
      return;
    }

    // Configure client with region if specified
    Aws::Client::ClientConfiguration config;
    if (!req.region().empty()) {
      config.region = req.region();
    }
    Aws::EC2::EC2Client ec2_client(config);

    Aws::EC2::Model::StartInstancesRequest start_request;
    start_request.AddInstanceIds(req.instance_id());

    auto outcome = ec2_client.StartInstances(start_request);

    res->set_instance_id(req.instance_id());
    if (outcome.IsSuccess()) {
      res->set_err_code(proto::ErrCode::Success);
      res->set_instance_status("starting");
      res->set_message("Instance start request submitted successfully");
      LOG(INFO) << "Successfully started instance: " << req.instance_id();
    } else {
      res->set_err_code(proto::ErrCode::Fail);
      res->set_message("Failed to start instance: " +
                       outcome.GetError().GetMessage());
      LOG(ERROR) << "Failed to start instance: " << req.instance_id() << " - "
                 << outcome.GetError().GetMessage();
    }
  }

  void HandleStopInstance(const proto::ServerRequest& req,
                          proto::ServerResponse* res) {
    if (req.instance_id().empty()) {
      res->set_err_code(proto::ErrCode::Fail);
      res->set_message("Instance ID is required for EC2 operations");
      return;
    }

    // Configure client with region if specified
    Aws::Client::ClientConfiguration config;
    if (!req.region().empty()) {
      config.region = req.region();
    }
    Aws::EC2::EC2Client ec2_client(config);

    Aws::EC2::Model::StopInstancesRequest stop_request;
    stop_request.AddInstanceIds(req.instance_id());

    auto outcome = ec2_client.StopInstances(stop_request);

    res->set_instance_id(req.instance_id());
    if (outcome.IsSuccess()) {
      res->set_err_code(proto::ErrCode::Success);
      res->set_instance_status("stopping");
      res->set_message("Instance stop request submitted successfully");
      LOG(INFO) << "Successfully stopped instance: " << req.instance_id();
    } else {
      res->set_err_code(proto::ErrCode::Fail);
      res->set_message("Failed to stop instance: " +
                       outcome.GetError().GetMessage());
      LOG(ERROR) << "Failed to stop instance: " << req.instance_id() << " - "
                 << outcome.GetError().GetMessage();
    }
  }

  bool aws_initialized_ = false;
};

}  // namespace grpc_handler
}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_GRPC_HANDLER_SERVER_HANDLER_H_
