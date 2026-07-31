/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/client/grpc_client.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>

#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "gtest/gtest.h"
#include "src/impl/config_manager.h"
#include "src/proto/service.grpc.pb.h"

namespace tbox {
namespace client {
namespace {

class GrpcClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grpc::ServerBuilder builder;
    int selected_port = 0;
    builder.RegisterService(&service_);
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(),
                             &selected_port);
    server_ = builder.BuildAndStart();
    ASSERT_NE(server_, nullptr);
    ASSERT_GT(selected_port, 0);

    config_path_ =
        (std::filesystem::current_path() / "grpc_client_test_config.json")
            .string();
    std::ofstream config(config_path_);
    ASSERT_TRUE(config.is_open());
    config << "{\n"
           << "  \"server_addr\": \"http://127.0.0.1\",\n"
           << "  \"grpc_server_port\": " << selected_port << ",\n"
           << "  \"check_interval_seconds\": 1,\n"
           << "  \"update_certs\": false\n"
           << "}\n";
    config.close();

    auto config_manager = util::ConfigManager::Instance();
    ASSERT_TRUE(config_manager->Init(config_path_));
  }

  void TearDown() override {
    if (client_ && client_->IsRunning()) {
      client_->Stop();
    }
    if (server_) {
      server_->Shutdown();
    }
    std::remove(config_path_.c_str());
  }

  std::unique_ptr<GrpcClient> client_;
  tbox::proto::TBOXService::Service service_;
  std::unique_ptr<grpc::Server> server_;
  std::string config_path_;
};

TEST_F(GrpcClientTest, ConstructWithoutConfig) {
  EXPECT_NO_THROW({ client_ = std::make_unique<GrpcClient>(); });
  EXPECT_NE(client_, nullptr);
}

TEST_F(GrpcClientTest, IsRunningInitiallyFalse) {
  client_ = std::make_unique<GrpcClient>();
  EXPECT_FALSE(client_->IsRunning());
}

TEST_F(GrpcClientTest, StartAndStop) {
  client_ = std::make_unique<GrpcClient>();

  // Start the client (will try to connect but may fail)
  client_->Start();

  // Give it a moment
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Should be running (even if connection fails, thread should be running)
  EXPECT_TRUE(client_->IsRunning());

  // Stop the client
  client_->Stop();

  // Give it a moment to stop
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Should not be running
  EXPECT_FALSE(client_->IsRunning());
}

TEST_F(GrpcClientTest, MultipleStartCalls) {
  client_ = std::make_unique<GrpcClient>();

  client_->Start();
  EXPECT_TRUE(client_->IsRunning());

  // Starting again should be safe
  client_->Start();
  EXPECT_TRUE(client_->IsRunning());

  client_->Stop();
}

TEST_F(GrpcClientTest, MultipleStopCalls) {
  client_ = std::make_unique<GrpcClient>();

  client_->Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  client_->Stop();
  EXPECT_FALSE(client_->IsRunning());

  // Stopping again should be safe
  client_->Stop();
  EXPECT_FALSE(client_->IsRunning());
}

TEST_F(GrpcClientTest, DeleteCopyOperations) {
  // Verify that copy operations are deleted
  // This is a compile-time test, if it compiles, the test passes
  EXPECT_TRUE(std::is_copy_constructible<GrpcClient>::value == false);
  EXPECT_TRUE(std::is_copy_assignable<GrpcClient>::value == false);
}

TEST_F(GrpcClientTest, ShortLivedConnection) {
  client_ = std::make_unique<GrpcClient>();

  client_->Start();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  client_->Stop();

  // Should handle quick start/stop without issues
  EXPECT_FALSE(client_->IsRunning());
}

}  // namespace
}  // namespace client
}  // namespace tbox
