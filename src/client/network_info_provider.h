/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_CLIENT_NETWORK_INFO_PROVIDER_H_
#define TBOX_CLIENT_NETWORK_INFO_PROVIDER_H_

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "glog/logging.h"

namespace tbox {
namespace client {

// RAII wrapper for ifaddrs to ensure proper cleanup
class IfAddrsGuard {
 public:
  explicit IfAddrsGuard(struct ifaddrs* ptr) : ptr_(ptr) {}
  ~IfAddrsGuard() {
    if (ptr_) {
      freeifaddrs(ptr_);
    }
  }
  
  // Delete copy operations
  IfAddrsGuard(const IfAddrsGuard&) = delete;
  IfAddrsGuard& operator=(const IfAddrsGuard&) = delete;
  
  struct ifaddrs* get() const { return ptr_; }

 private:
  struct ifaddrs* ptr_;
};

class NetworkInfoProvider {
 public:
  // Get all local IPv4 addresses (excluding loopback)
  static std::vector<std::string> GetLocalIPv4Addresses() {
    struct ifaddrs* ifaddrs_ptr = nullptr;
    std::vector<std::string> ip_addresses;

    if (getifaddrs(&ifaddrs_ptr) == -1) {
      LOG(ERROR) << "Failed to get network interfaces: " << strerror(errno);
      return ip_addresses;
    }

    IfAddrsGuard guard(ifaddrs_ptr);

    for (struct ifaddrs* ifa = ifaddrs_ptr; ifa != nullptr;
         ifa = ifa->ifa_next) {
      if (ifa->ifa_addr == nullptr) {
        continue;
      }

      // Check for IPv4 address
      if (ifa->ifa_addr->sa_family == AF_INET) {
        auto* sa = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
        char addr_buf[INET_ADDRSTRLEN];
        
        if (inet_ntop(AF_INET, &sa->sin_addr, addr_buf, INET_ADDRSTRLEN) ==
            nullptr) {
          LOG(WARNING) << "Failed to convert address for interface: "
                       << (ifa->ifa_name ? ifa->ifa_name : "unknown");
          continue;
        }

        std::string ip_str(addr_buf);
        
        // Skip loopback address
        if (ip_str == "127.0.0.1") {
          continue;
        }

        // Avoid duplicates
        if (std::find(ip_addresses.begin(), ip_addresses.end(), ip_str) ==
            ip_addresses.end()) {
          ip_addresses.push_back(ip_str);
          LOG(INFO) << "Detected IPv4 address: " << ip_str
                    << " on interface: "
                    << (ifa->ifa_name ? ifa->ifa_name : "unknown");
        }
      }
    }

    if (ip_addresses.empty()) {
      LOG(WARNING) << "No non-loopback IPv4 addresses found";
      ip_addresses.push_back("unknown");
    }

    return ip_addresses;
  }

  // Get all local IPv6 addresses (excluding loopback and link-local)
  static std::vector<std::string> GetLocalIPv6Addresses() {
    struct ifaddrs* ifaddrs_ptr = nullptr;
    std::vector<std::string> ip_addresses;

    if (getifaddrs(&ifaddrs_ptr) == -1) {
      LOG(ERROR) << "Failed to get network interfaces: " << strerror(errno);
      return ip_addresses;
    }

    IfAddrsGuard guard(ifaddrs_ptr);

    for (struct ifaddrs* ifa = ifaddrs_ptr; ifa != nullptr;
         ifa = ifa->ifa_next) {
      if (ifa->ifa_addr == nullptr) {
        continue;
      }

      // Check for IPv6 address
      if (ifa->ifa_addr->sa_family == AF_INET6) {
        auto* sa = reinterpret_cast<struct sockaddr_in6*>(ifa->ifa_addr);
        char addr_buf[INET6_ADDRSTRLEN];
        
        if (inet_ntop(AF_INET6, &sa->sin6_addr, addr_buf, INET6_ADDRSTRLEN) ==
            nullptr) {
          LOG(WARNING) << "Failed to convert IPv6 address for interface: "
                       << (ifa->ifa_name ? ifa->ifa_name : "unknown");
          continue;
        }

        std::string ip_str(addr_buf);
        
        // Skip loopback (::1) and link-local addresses (fe80::)
        if (ip_str == "::1" || ip_str.find("fe80:") == 0) {
          continue;
        }

        // Avoid duplicates
        if (std::find(ip_addresses.begin(), ip_addresses.end(), ip_str) ==
            ip_addresses.end()) {
          ip_addresses.push_back(ip_str);
          LOG(INFO) << "Detected IPv6 address: " << ip_str
                    << " on interface: "
                    << (ifa->ifa_name ? ifa->ifa_name : "unknown");
        }
      }
    }

    if (ip_addresses.empty()) {
      LOG(WARNING) << "No non-loopback/non-link-local IPv6 addresses found";
    }

    return ip_addresses;
  }

  // Get all local IP addresses (both IPv4 and IPv6)
  static std::vector<std::string> GetAllLocalIPAddresses() {
    std::vector<std::string> all_ips;
    
    // Get IPv4 addresses
    auto ipv4_addrs = GetLocalIPv4Addresses();
    all_ips.insert(all_ips.end(), ipv4_addrs.begin(), ipv4_addrs.end());
    
    // Get IPv6 addresses
    auto ipv6_addrs = GetLocalIPv6Addresses();
    all_ips.insert(all_ips.end(), ipv6_addrs.begin(), ipv6_addrs.end());
    
    return all_ips;
  }
};

}  // namespace client
}  // namespace tbox

#endif  // TBOX_CLIENT_NETWORK_INFO_PROVIDER_H_

