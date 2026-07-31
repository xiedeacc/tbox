/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/server/tcp_handler/vlmcsd_handler.h"

#include <cstring>
#include <sstream>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "src/common/logging.h"

extern "C" {
#include "src/kms.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#endif
#include "src/libkms.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
}

namespace tbox {
namespace server {
namespace tcp_handler {
namespace {

const char kEmbeddedEPid[] = {'T', 0, 'B', 0, 'O', 0, 'X', 0, 0, 0};

std::string JoinAddresses(const std::vector<std::string>& addresses) {
  std::ostringstream joined;
  for (size_t i = 0; i < addresses.size(); ++i) {
    if (i != 0) {
      joined << ",";
    }
    joined << addresses[i];
  }
  return joined.str();
}

uint32_t ToLittleEndian32(uint32_t value) {
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  return __builtin_bswap32(value);
#else
  return value;
#endif
}

bool IsLocalAddress(const std::string& address) {
  if (address == "127.0.0.1" || address == "::1") {
    return true;
  }
  if (address == "0.0.0.0" || address == "::") {
    return false;
  }

#if !defined(_WIN32)
  ifaddrs* interfaces = nullptr;
  if (getifaddrs(&interfaces) != 0) {
    return false;
  }

  bool found = false;
  for (ifaddrs* iface = interfaces; iface; iface = iface->ifa_next) {
    if (!iface->ifa_addr) {
      continue;
    }
    const int family = iface->ifa_addr->sa_family;
    if (family != AF_INET && family != AF_INET6) {
      continue;
    }

    char host[NI_MAXHOST] = {};
    if (getnameinfo(iface->ifa_addr,
                    family == AF_INET ? sizeof(sockaddr_in)
                                      : sizeof(sockaddr_in6),
                    host, sizeof(host), nullptr, 0, NI_NUMERICHOST) != 0) {
      continue;
    }
    if (address == host) {
      found = true;
      break;
    }
  }
  freeifaddrs(interfaces);
  return found;
#else
  return false;
#endif
}

void WakeKmsServer(const std::string& address, uint16_t port) {
#if !defined(_WIN32)
  addrinfo hints{};
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_NUMERICHOST;

  addrinfo* results = nullptr;
  const std::string port_text = std::to_string(port);
  if (getaddrinfo(address.c_str(), port_text.c_str(), &hints, &results) != 0) {
    return;
  }

  for (addrinfo* result = results; result; result = result->ai_next) {
    int socket_type = result->ai_socktype;
#if defined(SOCK_CLOEXEC)
    socket_type |= SOCK_CLOEXEC;
#endif
    const int fd =
        socket(result->ai_family, socket_type, result->ai_protocol);
    if (fd < 0) {
      continue;
    }
#if !defined(SOCK_CLOEXEC)
    const int descriptor_flags = fcntl(fd, F_GETFD);
    if (descriptor_flags >= 0) {
      fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC);
    }
#endif
    connect(fd, result->ai_addr, result->ai_addrlen);
    close(fd);
  }
  freeaddrinfo(results);
#else
  (void)address;
  (void)port;
#endif
}

HRESULT __stdcall KmsCallback(REQUEST* base_request, RESPONSE* base_response,
                              BYTE* hw_id, const char* ipstr) {
  LOG(INFO) << "vlmcsd activation request from "
            << (ipstr ? ipstr : "unknown");

  std::memcpy(&base_response->CMID, &base_request->CMID, sizeof(GUID));
  std::memcpy(&base_response->ClientTime, &base_request->ClientTime,
              sizeof(FILETIME));
  std::memcpy(&base_response->KmsPID, kEmbeddedEPid, sizeof(kEmbeddedEPid));

  base_response->Version = base_request->Version;
  base_response->Count =
      ToLittleEndian32(ToLittleEndian32(base_request->N_Policy) << 1);
  base_response->PIDSize = sizeof(kEmbeddedEPid);
  base_response->VLActivationInterval = ToLittleEndian32(120);
  base_response->VLRenewalInterval = ToLittleEndian32(10080);

  if (hw_id && base_response->MajorVer > 5) {
    std::memcpy(hw_id, "\x01\x02\x03\x04\x05\x06\x07\x08", 8);
  }

  return TRUE;
}

}  // namespace

VlmcsdHandler::VlmcsdHandler(uint16_t port)
    : listen_addresses_({"127.0.0.1"}), port_(port) {}

VlmcsdHandler::VlmcsdHandler(std::vector<std::string> listen_addresses,
                             uint16_t port)
    : listen_addresses_(std::move(listen_addresses)), port_(port) {}

VlmcsdHandler::~VlmcsdHandler() { Shutdown(); }

bool VlmcsdHandler::Start() {
  if (running_.exchange(true)) {
    LOG(WARNING) << "vlmcsd tcp handler is already running";
    return true;
  }
  if (listen_addresses_.empty()) {
    LOG(ERROR) << "vlmcsd listen addresses are not configured";
    running_.store(false);
    return false;
  }
  for (const auto& address : listen_addresses_) {
    if (!IsLocalAddress(address)) {
      LOG(ERROR) << "Refusing to start vlmcsd on non-local or wildcard "
                 << "address: " << address;
      running_.store(false);
      return false;
    }
  }

  server_thread_ = std::thread([this]() {
    const std::string listen_addresses = JoinAddresses(listen_addresses_);
    LOG(INFO) << "Starting vlmcsd TCP server on " << listen_addresses << ":"
              << port_;
    DWORD result =
        StartKmsServerWithAddress(listen_addresses.c_str(), port_, KmsCallback);
    if (result != 0) {
      LOG(ERROR) << "vlmcsd TCP server exited with error " << result << ": "
                 << GetErrorMessage();
    } else {
      LOG(INFO) << "vlmcsd TCP server stopped";
    }
    running_.store(false);
  });

  return true;
}

void VlmcsdHandler::Shutdown() {
  const bool was_running = running_.exchange(false);
  if (was_running) {
    LOG(INFO) << "Stopping vlmcsd TCP server";
    for (const auto& address : listen_addresses_) {
      WakeKmsServer(address, port_);
    }
    DWORD result = StopKmsServer();
    if (result != 0) {
      LOG(WARNING) << "StopKmsServer returned " << result << ": "
                   << GetErrorMessage();
    }
  }

  if (server_thread_.joinable()) {
    server_thread_.join();
  }
}

}  // namespace tcp_handler
}  // namespace server
}  // namespace tbox
