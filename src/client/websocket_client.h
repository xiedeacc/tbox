/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_CLIENT_WEBSOCKET_CLIENT_H_
#define TBOX_CLIENT_WEBSOCKET_CLIENT_H_

#include <memory>
#include <string>

#include "boost/asio/connect.hpp"
#include "boost/asio/io_context.hpp"
#include "boost/asio/ip/tcp.hpp"
#include "boost/beast/core/buffers_to_string.hpp"
#include "boost/beast/websocket/stream.hpp"
#include "folly/MPMCQueue.h"
#include "folly/executors/CPUThreadPoolExecutor.h"
#include "glog/logging.h"
#include "src/proto/service.pb.h"

namespace tbox {
namespace client {

/// @brief Manages WebSocket connections and message processing.
/// @details Handles async WebSocket communication with message
///          queuing and multi-threaded processing via Boost.Beast.
class WebSocketClient {
 public:
  /// @brief Constructor with host and port.
  /// @param host Server hostname or IP address.
  /// @param port Server port number as string.
  WebSocketClient(const std::string& host, const std::string& port);

  ~WebSocketClient();

  /// @brief Connect to WebSocket server.
  /// @throws std::exception on connection failure.
  void Connect();

  /// @brief Start I/O service and message processing threads.
  void Start();

  /// @brief Stop WebSocket client and cleanup resources.
  void Stop();

  /// @brief Send message to WebSocket server.
  /// @param message Message content to send.
  /// @param is_binary True for binary, false for text (default: true).
  void SendMessage(const std::string& message, bool is_binary = true);

 private:
  /// @brief Process messages from the queue.
  void ProcessMessages();

  /// @brief Handle a single message.
  /// @param msg Message to process.
  void HandleMessage(const std::string& msg);

  /// @brief Start reading messages asynchronously.
  void ReadMessage();
  std::string host_;
  std::string port_;
  boost::asio::io_context ioc_;
  boost::beast::websocket::stream<boost::asio::ip::tcp::socket> ws_;

  folly::MPMCQueue<std::string> message_queue_;
  std::atomic<bool> running_;
  std::shared_ptr<folly::CPUThreadPoolExecutor> thread_pool_;
  boost::beast::flat_buffer read_buffer_;
  std::thread io_thread_;
};

}  // namespace client
}  // namespace tbox

#endif  // TBOX_CLIENT_WEBSOCKET_CLIENT_H_
