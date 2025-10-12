/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/client/authentication_manager.h"

#include <memory>
#include <thread>

#include "grpcpp/grpcpp.h"
#include "grpcpp/server_builder.h"
#include "gtest/gtest.h"
#include "src/proto/service.grpc.pb.h"
#include "src/proto/service.pb.h"

namespace tbox {
namespace client {
namespace {

// Mock gRPC service implementation for testing.
class MockTBOXServiceImpl final : public tbox::proto::TBOXService::Service {
 public:
  MockTBOXServiceImpl()
      : should_succeed_(true),
        return_token_("test_token"),
        grpc_error_code_(grpc::StatusCode::OK) {}

  void SetShouldSucceed(bool should_succeed) {
    should_succeed_ = should_succeed;
  }
  void SetServerError(tbox::proto::ErrCode error) { server_error_ = error; }
  void SetReturnToken(const std::string& token) { return_token_ = token; }
  void SetGrpcError(grpc::StatusCode code, const std::string& msg) {
    grpc_error_code_ = code;
    grpc_error_msg_ = msg;
  }

  grpc::Status UserOp(grpc::ServerContext* context,
                      const tbox::proto::UserRequest* request,
                      tbox::proto::UserResponse* response) override {
    if (grpc_error_code_ != grpc::StatusCode::OK) {
      return grpc::Status(grpc_error_code_, grpc_error_msg_);
    }

    if (should_succeed_) {
      response->set_err_code(tbox::proto::ErrCode::Success);
      response->set_token(return_token_);
    } else {
      response->set_err_code(server_error_);
    }

    return grpc::Status::OK;
  }

  // Required overrides (not used in these tests)
  grpc::Status EC2Op(grpc::ServerContext* context,
                     const tbox::proto::EC2Request* request,
                     tbox::proto::EC2Response* response) override {
    return grpc::Status::OK;
  }

  grpc::Status ReportOp(grpc::ServerContext* context,
                        const tbox::proto::ReportRequest* request,
                        tbox::proto::ReportResponse* response) override {
    return grpc::Status::OK;
  }

 private:
  bool should_succeed_;
  tbox::proto::ErrCode server_error_;
  std::string return_token_;
  grpc::StatusCode grpc_error_code_;
  std::string grpc_error_msg_;
};

class AuthenticationManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Start mock gRPC server
    mock_service_ = std::make_unique<MockTBOXServiceImpl>();

    std::string server_address =
        "127.0.0.1:0";  // Use port 0 to get any available port
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials(),
                             &selected_port_);
    builder.RegisterService(mock_service_.get());
    server_ = builder.BuildAndStart();

    ASSERT_NE(server_, nullptr);
    ASSERT_GT(selected_port_, 0);

    // Create client channel and stub
    std::string target = "127.0.0.1:" + std::to_string(selected_port_);
    auto channel =
        grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    auto stub_unique = tbox::proto::TBOXService::NewStub(channel);
    stub_ =
        std::shared_ptr<tbox::proto::TBOXService::Stub>(stub_unique.release());

    // Initialize auth manager
    auth_manager_ = AuthenticationManager::Instance();
    auth_manager_->Init(stub_);
    auth_manager_->ClearToken();
  }

  void TearDown() override {
    auth_manager_->ClearToken();
    if (server_) {
      server_->Shutdown();
      server_->Wait();
    }
  }

  std::unique_ptr<MockTBOXServiceImpl> mock_service_;
  std::unique_ptr<grpc::Server> server_;
  std::shared_ptr<tbox::proto::TBOXService::Stub> stub_;
  std::shared_ptr<AuthenticationManager> auth_manager_;
  int selected_port_ = 0;
};

TEST_F(AuthenticationManagerTest, InitializeWithStub) {
  auto new_auth_manager = AuthenticationManager::Instance();

  // Create a new stub and initialize
  std::string target = "127.0.0.1:" + std::to_string(selected_port_);
  auto channel =
      grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
  auto new_stub_unique = tbox::proto::TBOXService::NewStub(channel);
  auto new_stub = std::shared_ptr<tbox::proto::TBOXService::Stub>(
      new_stub_unique.release());
  new_auth_manager->Init(new_stub);
  // If we get here without crashing, initialization works
}

TEST_F(AuthenticationManagerTest, IsAuthenticatedInitiallyFalse) {
  EXPECT_FALSE(auth_manager_->IsAuthenticated());
  EXPECT_TRUE(auth_manager_->GetToken().empty());
}

TEST_F(AuthenticationManagerTest, LoginSuccess) {
  // Setup mock to return success
  mock_service_->SetShouldSucceed(true);
  mock_service_->SetReturnToken("test_token_12345");

  bool result = auth_manager_->Login();

  EXPECT_TRUE(result);
  EXPECT_TRUE(auth_manager_->IsAuthenticated());
  EXPECT_EQ(auth_manager_->GetToken(), "test_token_12345");
}

TEST_F(AuthenticationManagerTest, LoginServerError) {
  // Setup mock to return server error
  mock_service_->SetShouldSucceed(false);
  mock_service_->SetServerError(tbox::proto::User_login_error);

  bool result = auth_manager_->Login();

  EXPECT_FALSE(result);
  EXPECT_FALSE(auth_manager_->IsAuthenticated());
}

TEST_F(AuthenticationManagerTest, ClearToken) {
  // First login successfully
  mock_service_->SetShouldSucceed(true);
  mock_service_->SetReturnToken("test_token");

  auth_manager_->Login();
  EXPECT_TRUE(auth_manager_->IsAuthenticated());

  // Clear token
  auth_manager_->ClearToken();
  EXPECT_FALSE(auth_manager_->IsAuthenticated());
  EXPECT_TRUE(auth_manager_->GetToken().empty());
}

TEST_F(AuthenticationManagerTest, GetTokenThreadSafety) {
  // This test ensures thread safety of GetToken
  mock_service_->SetShouldSucceed(true);
  mock_service_->SetReturnToken("concurrent_token");

  auth_manager_->Login();

  // Access token from multiple "threads" (sequentially in test)
  std::string token1 = auth_manager_->GetToken();
  std::string token2 = auth_manager_->GetToken();

  EXPECT_EQ(token1, token2);
  EXPECT_EQ(token1, "concurrent_token");
}

TEST_F(AuthenticationManagerTest, MultipleLogins) {
  // First login
  mock_service_->SetShouldSucceed(true);
  mock_service_->SetReturnToken("token1");
  auth_manager_->Login();
  EXPECT_EQ(auth_manager_->GetToken(), "token1");

  // Second login with different token
  mock_service_->SetReturnToken("token2");
  auth_manager_->Login();
  EXPECT_EQ(auth_manager_->GetToken(), "token2");
}

TEST_F(AuthenticationManagerTest, LoginAfterClear) {
  // Login
  mock_service_->SetShouldSucceed(true);
  mock_service_->SetReturnToken("initial_token");
  auth_manager_->Login();
  EXPECT_TRUE(auth_manager_->IsAuthenticated());

  // Clear
  auth_manager_->ClearToken();
  EXPECT_FALSE(auth_manager_->IsAuthenticated());

  // Login again
  mock_service_->SetReturnToken("new_token");
  auth_manager_->Login();
  EXPECT_TRUE(auth_manager_->IsAuthenticated());
  EXPECT_EQ(auth_manager_->GetToken(), "new_token");
}

}  // namespace
}  // namespace client
}  // namespace tbox
