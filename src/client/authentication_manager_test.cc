/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include "src/client/authentication_manager.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "grpcpp/grpcpp.h"
#include "grpcpp/server_builder.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#include "gtest/gtest.h"
#include "src/common/ssh_key_auth.h"
#include "src/impl/config_manager.h"
#include "src/proto/service.grpc.pb.h"
#include "src/proto/service.pb.h"

namespace tbox {
namespace client {
namespace {

// Mock gRPC service implementation for testing.
class MockTBOXServiceImpl final : public tbox::proto::TBOXService::Service {
 public:
  MockTBOXServiceImpl(std::string private_key_path,
                      std::string public_key_path)
      : should_succeed_(true),
        server_error_(tbox::proto::User_login_error),
        return_token_("test_token"),
        grpc_error_code_(grpc::StatusCode::OK),
        private_key_path_(std::move(private_key_path)),
        public_key_path_(std::move(public_key_path)) {}

  void SetShouldSucceed(bool should_succeed) {
    should_succeed_ = should_succeed;
  }
  void SetServerError(tbox::proto::ErrCode error) { server_error_ = error; }
  void SetReturnToken(const std::string& token) { return_token_ = token; }
  void SetGrpcError(grpc::StatusCode code, const std::string& msg) {
    grpc_error_code_ = code;
    grpc_error_msg_ = msg;
  }

  grpc::Status UserOp(grpc::ServerContext*,
                      const tbox::proto::UserRequest* request,
                      tbox::proto::UserResponse* response) override {
    if (grpc_error_code_ != grpc::StatusCode::OK) {
      return grpc::Status(grpc_error_code_, grpc_error_msg_);
    }

    if (!should_succeed_) {
      response->set_err_code(server_error_);
      return grpc::Status::OK;
    }

    response->set_err_code(tbox::proto::ErrCode::Success);
    if (request->op() == tbox::proto::OP_USER_LOGIN_CHALLENGE) {
      constexpr char kChallengeId[] = "authentication-test-challenge";
      const std::string server_nonce(32, 'S');
      const std::string message = tbox::common::SshKeyAuth::BuildLoginMessage(
          kChallengeId, request->client_nonce(), server_nonce, request->user(),
          request->request_id());
      std::string server_signature;
      if (!tbox::common::SshKeyAuth::CreateServerProof(
              private_key_path_, public_key_path_, message,
              &server_signature)) {
        return grpc::Status(grpc::StatusCode::INTERNAL,
                            "failed to create test server proof");
      }

      response->set_challenge_id(kChallengeId);
      response->set_server_nonce(server_nonce);
      response->set_server_signature(server_signature);
      const auto expires_at =
          std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count() +
          60;
      response->set_challenge_expires_at(expires_at);
    } else if (request->op() == tbox::proto::OP_USER_LOGIN) {
      response->set_token(return_token_);
    } else {
      response->set_err_code(tbox::proto::Unsupported_op);
    }

    return grpc::Status::OK;
  }

  // Required overrides (not used in these tests)
  grpc::Status EC2Op(grpc::ServerContext*,
                     const tbox::proto::EC2Request*,
                     tbox::proto::EC2Response*) override {
    return grpc::Status::OK;
  }

  grpc::Status ReportOp(grpc::ServerContext*,
                        const tbox::proto::ReportRequest*,
                        tbox::proto::ReportResponse*) override {
    return grpc::Status::OK;
  }

 private:
  bool should_succeed_;
  tbox::proto::ErrCode server_error_;
  std::string return_token_;
  grpc::StatusCode grpc_error_code_;
  std::string grpc_error_msg_;
  std::string private_key_path_;
  std::string public_key_path_;
};

class AuthenticationManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto test_dir = std::filesystem::current_path();
    private_key_path_ =
        test_dir / "authentication_manager_test_id_ed25519";
    public_key_path_ =
        test_dir / "authentication_manager_test_id_ed25519.pub";
    config_path_ = test_dir / "authentication_manager_test_config.json";

    {
      std::ofstream private_key(private_key_path_, std::ios::binary);
      private_key << "test-private-key-material\n";
    }
    {
      std::ofstream public_key(public_key_path_, std::ios::binary);
      public_key
          << "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAITestKey authentication-test\n";
    }
    {
      std::ofstream config(config_path_, std::ios::binary);
      config << "{\n"
             << "  \"server_addr\": \"http://127.0.0.1\",\n"
             << "  \"grpc_server_port\": 1,\n"
             << "  \"user\": \"test-user\",\n"
             << "  \"password\": \"test-password\",\n"
             << "  \"client_id\": \"test-client\",\n"
             << "  \"ssh_private_key_path\": \""
             << private_key_path_.generic_string() << "\",\n"
             << "  \"ssh_public_key_path\": \""
             << public_key_path_.generic_string() << "\"\n"
             << "}\n";
    }
    ASSERT_TRUE(tbox::util::ConfigManager::Instance()->Init(
        config_path_.string()));

    // Start mock gRPC server
    mock_service_ = std::make_unique<MockTBOXServiceImpl>(
        private_key_path_.string(), public_key_path_.string());

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
    std::error_code error;
    std::filesystem::remove(config_path_, error);
    std::filesystem::remove(private_key_path_, error);
    std::filesystem::remove(public_key_path_, error);
  }

  std::unique_ptr<MockTBOXServiceImpl> mock_service_;
  std::unique_ptr<grpc::Server> server_;
  std::shared_ptr<tbox::proto::TBOXService::Stub> stub_;
  std::shared_ptr<AuthenticationManager> auth_manager_;
  std::filesystem::path config_path_;
  std::filesystem::path private_key_path_;
  std::filesystem::path public_key_path_;
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
