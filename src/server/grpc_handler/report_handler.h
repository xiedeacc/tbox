/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_GRPC_HANDLER_REPORT_HANDLER_H_
#define TBOX_SERVER_GRPC_HANDLER_REPORT_HANDLER_H_

#include <memory>
#include <string>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "glog/logging.h"
#include "src/async_grpc/rpc_handler.h"
#include "src/server/grpc_handler/meta.h"

namespace tbox {
namespace server {
namespace grpc_handler {

class ReportOpHandler : public async_grpc::RpcHandler<ReportOpMethod> {
 public:
  ReportOpHandler() = default;
  ~ReportOpHandler() = default;

  void OnRequest(const proto::ReportRequest& req) override {
    auto res = std::make_unique<proto::ReportResponse>();
    
    // Copy all client IPs to response
    for (const auto& ip : req.client_ip()) {
      res->add_client_ip(ip);
    }

    // Create a string representation of all IPs for logging
    std::ostringstream ip_stream;
    for (int i = 0; i < req.client_ip_size(); ++i) {
      if (i > 0) ip_stream << ", ";
      ip_stream << req.client_ip(i);
    }
    std::string all_ips = ip_stream.str();

    LOG(INFO) << "Report request: " << req.op()
              << " from client IPs: [" << all_ips << "]"
              << " timestamp: " << req.timestamp()
              << " client_info: " << req.client_info();

    try {
      // Determine the operation based on the request op code
      switch (req.op()) {
        case proto::OpCode::OP_REPORT:
          HandleReport(req, res.get());
          break;
        default:
          res->set_err_code(proto::ErrCode::FAIL);
          res->set_message("Invalid operation code for Report");
          LOG(ERROR) << "Invalid operation code: " << req.op();
          break;
      }
    } catch (const std::exception& e) {
      LOG(ERROR) << "Report operation failed: " << e.what();
      res->set_err_code(proto::ErrCode::FAIL);
      res->set_message(std::string("Operation failed: ") + e.what());
    }

    Send(std::move(res));
  }

  void OnReadsDone() override { Finish(grpc::Status::OK); }

 private:
  void HandleReport(const proto::ReportRequest& req,
                   proto::ReportResponse* res) {
    // Get current server time
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S UTC");
    std::string server_time = ss.str();

    // Create a string representation of all IPs for logging
    std::ostringstream ip_stream;
    for (int i = 0; i < req.client_ip_size(); ++i) {
      if (i > 0) ip_stream << ", ";
      ip_stream << req.client_ip(i);
    }
    std::string all_ips = ip_stream.str();

    // Log the client IP report
    LOG(INFO) << "Client IP report received: " 
              << "IPs=[" << all_ips << "]"
              << ", Client Time=" << req.timestamp()
              << ", Server Time=" << server_time
              << ", Client Info=" << req.client_info();

    // Set response fields
    res->set_err_code(proto::ErrCode::SUCCESS);
    res->set_server_time(server_time);
    
    std::ostringstream msg_stream;
    msg_stream << "Client IP report received successfully. " 
               << req.client_ip_size() << " IP address(es) reported: [" << all_ips << "]";
    res->set_message(msg_stream.str());

    LOG(INFO) << "Successfully processed IP report from client with " 
              << req.client_ip_size() << " IP addresses: [" << all_ips << "]";
  }
};

}  // namespace grpc_handler
}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_GRPC_HANDLER_REPORT_HANDLER_H_
