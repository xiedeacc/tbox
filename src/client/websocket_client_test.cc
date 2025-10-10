/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/client/websocket_client.h"

#include <chrono>
#include <thread>

#include "gtest/gtest.h"
#include "src/proto/service.pb.h"
#include "src/util/util.h"

namespace tbox {
namespace client {
namespace {

class WebSocketClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    client_ = std::make_unique<WebSocketClient>("127.0.0.1", "10003");
  }

  void TearDown() override {
    if (client_) {
      client_->Stop();
    }
  }

  std::unique_ptr<WebSocketClient> client_;
};

TEST_F(WebSocketClientTest, ConstructWithHostAndPort) {
  // Just verify we can construct the client without crashes
  EXPECT_NE(client_, nullptr);
}

TEST_F(WebSocketClientTest, StopWithoutStart) {
  // Stop should be safe to call even if not started
  EXPECT_NO_THROW(client_->Stop());
}

TEST_F(WebSocketClientTest, MultipleStopCalls) {
  // Multiple stops should be safe
  client_->Stop();
  EXPECT_NO_THROW(client_->Stop());
}

// Note: Real connection tests (ConnectAndDisconnect) disabled
// because they require an actual WebSocket server running.
// These can be enabled in integration tests with server setup.
TEST_F(WebSocketClientTest, DISABLED_ConnectAndDisconnect) {
  EXPECT_NO_THROW(client_->Connect());
  std::this_thread::sleep_for(std::chrono::seconds(1));
}

}  // namespace
}  // namespace client
}  // namespace tbox
