/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_SERVER_TCP_HANDLER_VLMCSD_HANDLER_H_
#define TBOX_SERVER_TCP_HANDLER_VLMCSD_HANDLER_H_

#include <atomic>
#include <cstdint>
#include <thread>

namespace tbox {
namespace server {
namespace tcp_handler {

class VlmcsdHandler {
 public:
  explicit VlmcsdHandler(uint16_t port = 1688);
  VlmcsdHandler(const VlmcsdHandler&) = delete;
  VlmcsdHandler& operator=(const VlmcsdHandler&) = delete;
  ~VlmcsdHandler();

  bool Start();
  void Shutdown();

 private:
  uint16_t port_;
  std::atomic_bool running_{false};
  std::thread server_thread_;
};

}  // namespace tcp_handler
}  // namespace server
}  // namespace tbox

#endif  // TBOX_SERVER_TCP_HANDLER_VLMCSD_HANDLER_H_
