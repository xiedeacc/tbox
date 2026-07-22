/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/server/tcp_handler/vlmcsd_handler.h"

#include <cstring>

#include "src/common/logging.h"

extern "C" {
#include "kms.h"
#include "libkms.h"
}

namespace tbox {
namespace server {
namespace tcp_handler {
namespace {

const char kEmbeddedEPid[] = {'T', 0, 'B', 0, 'O', 0, 'X', 0, 0, 0};

uint32_t ToLittleEndian32(uint32_t value) {
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  return __builtin_bswap32(value);
#else
  return value;
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

VlmcsdHandler::VlmcsdHandler(uint16_t port) : port_(port) {}

VlmcsdHandler::~VlmcsdHandler() { Shutdown(); }

bool VlmcsdHandler::Start() {
  if (running_.exchange(true)) {
    LOG(WARNING) << "vlmcsd tcp handler is already running";
    return true;
  }

  server_thread_ = std::thread([this]() {
    LOG(INFO) << "Starting vlmcsd TCP server on port " << port_;
    DWORD result = StartKmsServer(port_, KmsCallback);
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
