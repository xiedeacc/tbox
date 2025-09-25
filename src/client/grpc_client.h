/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_CLIENT_GRPC_CLIENT_H_
#define TBOX_CLIENT_GRPC_CLIENT_H_

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <ifaddrs.h>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include <vector>

#include "glog/logging.h"
#include "grpcpp/grpcpp.h"
#include "src/async_grpc/client.h"
#include "src/server/grpc_handler/meta.h"

namespace tbox {
namespace client {

class GrpcClient {
 public:
  GrpcClient(const std::string& host, int port, int report_interval_seconds = 30)
      : host_(host),
        port_(port),
        target_address_(host + ":" + std::to_string(port)),
        report_interval_seconds_(report_interval_seconds),
        running_(false),
        should_stop_(false) {
    channel_ = grpc::CreateChannel(target_address_, grpc::InsecureChannelCredentials());
  }

  ~GrpcClient() { 
    Stop();
  }

  // Get all local IP addresses of this client
  std::vector<std::string> GetLocalIPAddresses() {
    struct ifaddrs *ifaddrs_ptr;
    std::vector<std::string> ip_addresses;
    
    if (getifaddrs(&ifaddrs_ptr) == -1) {
      LOG(ERROR) << "Failed to get network interfaces";
      return ip_addresses;
    }

    for (struct ifaddrs *ifa = ifaddrs_ptr; ifa != nullptr; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr == nullptr) continue;
      
      // Check for IPv4 address
      if (ifa->ifa_addr->sa_family == AF_INET) {
        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        char *addr = inet_ntoa(sa->sin_addr);
        
        // Skip loopback address but include all other interfaces
        if (strcmp(addr, "127.0.0.1") != 0) {
          std::string ip_str(addr);
          // Avoid duplicates
          if (std::find(ip_addresses.begin(), ip_addresses.end(), ip_str) == ip_addresses.end()) {
            ip_addresses.push_back(ip_str);
            LOG(INFO) << "Detected IP address: " << ip_str << " on interface: " << ifa->ifa_name;
          }
        }
      }
    }

    freeifaddrs(ifaddrs_ptr);
    
    if (ip_addresses.empty()) {
      LOG(WARNING) << "No non-loopback IP addresses found, adding 'unknown'";
      ip_addresses.push_back("unknown");
    }
    
    return ip_addresses;
  }

  // Get the first local IP address (for backward compatibility)
  std::string GetLocalIPAddress() {
    auto ip_addresses = GetLocalIPAddresses();
    return ip_addresses.empty() ? "unknown" : ip_addresses[0];
  }

  // Report all client IP addresses to the server
  bool ReportClientIP() {
    std::vector<std::string> client_ips = GetLocalIPAddresses();
    return ReportClientIPs(client_ips);
  }

  // Report a specific client IP address to the server (backward compatibility)
  bool ReportClientIP(const std::string& client_ip) {
    std::vector<std::string> client_ips = {client_ip};
    return ReportClientIPs(client_ips);
  }

  // Report multiple client IP addresses to the server
  bool ReportClientIPs(const std::vector<std::string>& client_ips) {
    try {
      async_grpc::Client<tbox::server::grpc_handler::ReportOpMethod> client(channel_);
      
      tbox::proto::ReportRequest request;
      request.set_op(tbox::proto::OpCode::OP_REPORT);
      
      // Add all client IP addresses
      for (const auto& ip : client_ips) {
        request.add_client_ip(ip);
      }
      
      request.set_timestamp(std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch()).count());
      request.set_client_info("TBox C++ Client");

      grpc::Status status;
      bool success = client.Write(request, &status);
      
      if (success && status.ok()) {
        const auto& response = client.response();
        
        // Create a string representation of all reported IPs for logging
        std::ostringstream ip_stream;
        for (size_t i = 0; i < client_ips.size(); ++i) {
          if (i > 0) ip_stream << ", ";
          ip_stream << client_ips[i];
        }
        std::string all_ips = ip_stream.str();
        
        LOG(INFO) << "Successfully reported " << client_ips.size() 
                  << " client IP(s): [" << all_ips << "] to server " << target_address_
                  << ". Server response: " << response.message()
                  << " Server time: " << response.server_time();
        return true;
      } else {
        std::ostringstream ip_stream;
        for (size_t i = 0; i < client_ips.size(); ++i) {
          if (i > 0) ip_stream << ", ";
          ip_stream << client_ips[i];
        }
        std::string all_ips = ip_stream.str();
        
        LOG(ERROR) << "Failed to report " << client_ips.size() 
                   << " client IP(s): [" << all_ips << "] to " << target_address_
                   << ". gRPC status: " << status.error_code() 
                   << " - " << status.error_message();
        return false;
      }
    } catch (const std::exception& e) {
      std::ostringstream ip_stream;
      for (size_t i = 0; i < client_ips.size(); ++i) {
        if (i > 0) ip_stream << ", ";
        ip_stream << client_ips[i];
      }
      std::string all_ips = ip_stream.str();
      
      LOG(ERROR) << "Exception while reporting " << client_ips.size() 
                 << " client IP(s): [" << all_ips << "] to " << target_address_
                 << ": " << e.what();
      return false;
    }
  }

  // Check if the gRPC channel is ready
  bool IsChannelReady() {
    auto state = channel_->GetState(false);
    return state == GRPC_CHANNEL_READY;
  }

  // Wait for the channel to be ready with timeout
  bool WaitForChannelReady(int timeout_seconds = 5) {
    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(timeout_seconds);
    return channel_->WaitForConnected(deadline);
  }

  // Get connection info
  const std::string& GetTargetAddress() const {
    return target_address_;
  }

  // Start the background thread for periodic IP reporting
  void StartReporting() {
    if (running_) {
      LOG(WARNING) << "IP reporting thread is already running";
      return;
    }

    should_stop_ = false;
    running_ = true;
    
    reporting_thread_ = std::thread(&GrpcClient::ReportingLoop, this);
    LOG(INFO) << "Started IP reporting thread with interval " << report_interval_seconds_ << " seconds";
  }

  // Stop the background thread
  void Stop() {
    if (!running_) {
      return;
    }

    LOG(INFO) << "Stopping IP reporting thread...";
    
    // Signal the thread to stop
    {
      std::lock_guard<std::mutex> lock(mutex_);
      should_stop_ = true;
    }
    cv_.notify_all();

    // Wait for the thread to finish
    if (reporting_thread_.joinable()) {
      reporting_thread_.join();
    }

    running_ = false;
    LOG(INFO) << "IP reporting thread stopped";
  }

  // Check if reporting thread is running
  bool IsReporting() const {
    return running_;
  }

  // Set the reporting interval (only effective if stopped)
  void SetReportInterval(int seconds) {
    if (!running_) {
      report_interval_seconds_ = seconds;
      LOG(INFO) << "Set reporting interval to " << seconds << " seconds";
    } else {
      LOG(WARNING) << "Cannot change reporting interval while thread is running";
    }
  }

 private:
  // The main loop that runs in the background thread
  void ReportingLoop() {
    LOG(INFO) << "IP reporting loop started";
    
    while (true) {
      // Check if we should stop
      {
        std::unique_lock<std::mutex> lock(mutex_);
        if (cv_.wait_for(lock, std::chrono::seconds(report_interval_seconds_), 
                         [this] { return should_stop_.load(); })) {
          // should_stop_ became true, exit loop
          break;
        }
      }

      // Report client IP addresses
      try {
        std::vector<std::string> client_ips = GetLocalIPAddresses();
        bool success = ReportClientIPs(client_ips);
        
        if (!success) {
          LOG(WARNING) << "Failed to report client IP in background thread, will retry in " 
                       << report_interval_seconds_ << " seconds";
        }
      } catch (const std::exception& e) {
        LOG(ERROR) << "Exception in IP reporting loop: " << e.what();
      }
    }
    
    LOG(INFO) << "IP reporting loop ended";
  }

  std::string host_;
  int port_;
  std::string target_address_;
  std::shared_ptr<grpc::Channel> channel_;
  
  // Thread management
  int report_interval_seconds_;
  std::atomic<bool> running_;
  std::atomic<bool> should_stop_;
  std::thread reporting_thread_;
  std::mutex mutex_;
  std::condition_variable cv_;
};

}  // namespace client
}  // namespace tbox

#endif  // TBOX_CLIENT_GRPC_CLIENT_H_
