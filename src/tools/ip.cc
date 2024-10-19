/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include <chrono>
#include <string>

#include "folly/init/Init.h"
#include "glog/logging.h"
#include "grpcpp/client_context.h"
#include "grpcpp/grpcpp.h"
#include "grpcpp/support/client_callback.h"
#include "src/proto/service.grpc.pb.h"
#include "src/proto/service.pb.h"
#include "src/util/config_manager.h"
#include "src/util/util.h"

using tbox::util::ConfigManager;

class GrpcClient {
 public:
  explicit GrpcClient(const std::string& addr, const std::string& port)
      : channel_(grpc::CreateChannel(addr + ":" + port,
                                     grpc::InsecureChannelCredentials())),
        stub_(tbox::proto::TboxService::NewStub(channel_)) {
    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(10);
    if (channel_->WaitForConnected(deadline)) {
      LOG(INFO) << "Connect to " << addr << ":" << port << " success";
    }
  }

  bool DoRpc(const tbox::proto::UserReq& req, tbox::proto::UserRes* res) {
    grpc::ClientContext context;
    auto status = stub_->UserOp(&context, req, res);
    if (!status.ok()) {
      LOG(ERROR) << "Grpc error";
      return false;
    }
    if (res->err_code()) {
      LOG(ERROR) << "Server error: " << res->err_code();
      return false;
    }
    return true;
  }

  bool DoRpc(const tbox::proto::ServerReq& req, tbox::proto::ServerRes* res) {
    grpc::ClientContext context;
    auto status = stub_->ServerOp(&context, req, res);
    if (!status.ok()) {
      LOG(ERROR) << "Grpc error";
      return false;
    }
    if (res->err_code()) {
      LOG(ERROR) << "Server error: " << res->err_code();
      return false;
    }
    return true;
  }

  bool Login() {
    tbox::proto::UserReq user_req;
    tbox::proto::UserRes user_res;
    user_req.set_request_id(tbox::util::Util::UUID());
    user_req.set_op(tbox::proto::UserOp::UserLogin);
    user_req.set_user(ConfigManager::Instance()->User());
    user_req.set_password(ConfigManager::Instance()->Password());
    if (!DoRpc(user_req, &user_res)) {
      LOG(ERROR) << "Login error";
      return false;
    }
    token_ = user_res.token();
    LOG(INFO) << "Login success, token: " << token_;
    return true;
  }

  bool GetDevIp() {
    if (token_.empty()) {
      if (!Login()) {
        return false;
      }
    }

    tbox::proto::ServerReq server_req;
    tbox::proto::ServerRes server_res;
    server_req.set_request_id(tbox::util::Util::UUID());
    server_req.set_op(tbox::proto::ServerOp::ServerGetDevIp);
    server_req.set_user(ConfigManager::Instance()->User());
    server_req.set_token(token_);
    if (!DoRpc(server_req, &server_res)) {
      LOG(ERROR) << "Login error";
      return false;
    }

    LOG(INFO) << "GetDevIp success, list: \n" << server_res.err_msg();

    return true;
  }

 private:
  std::shared_ptr<grpc::Channel> channel_;
  std::unique_ptr<tbox::proto::TboxService::Stub> stub_;
  std::string token_;
};

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv, false);
  google::SetStderrLogging(google::GLOG_INFO);
  gflags::ParseCommandLineFlags(&argc, &argv, false);

  tbox::util::ConfigManager::Instance()->Init("./conf/client_base_config.json");
  GrpcClient grpc_client(
      ConfigManager::Instance()->ServerAddr(),
      std::to_string(ConfigManager::Instance()->GrpcServerPort()));
  grpc_client.GetDevIp();
  return 0;
}
