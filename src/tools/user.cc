/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#include <chrono>
#include <fstream>
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

DEFINE_string(op, "register", "regist, delete, changepw");

using tbox::util::ConfigManager;

class GrpcClient {
 public:
  explicit GrpcClient(const std::string& addr, const std::string& port) {
    grpc::SslCredentialsOptions ssl_opts;
    ssl_opts.pem_root_certs = LoadCACert("/conf/xiedeacc.com.ca.cer");
    ssl_opts.pem_cert_chain = LoadCACert("/conf/xiedeacc.com.fullchain.cer");
    // auto channel_creds = grpc::SslCredentials(ssl_opts);

    auto channel_creds = grpc::SslCredentials(grpc::SslCredentialsOptions());

    grpc::ChannelArguments args;
    args.SetSslTargetNameOverride("xiedeacc.com");
    args.SetString(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG, "xiedeacc.com");

    channel_ =
        grpc::CreateChannel(addr + ":80", grpc::InsecureChannelCredentials());
    // channel_ = grpc::CreateChannel(addr + ":" + port, channel_creds);
    // channel_ =
    // grpc::CreateCustomChannel(addr + ":" + port, channel_creds, args);
    stub_ = tbox::proto::TboxService::NewStub(channel_);

    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(10);
    if (channel_->WaitForConnected(deadline)) {
      LOG(INFO) << "Connect to " << addr << ":" << port << " success";
    }
  }

  std::string LoadCACert(const std::string& path) {
    std::string home_dir = tbox::util::Util::HomeDir();
    std::ifstream file(home_dir + path);
    std::string ca_cert((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    LOG(INFO) << ca_cert;
    return ca_cert;
  }

  bool DoRpc(const tbox::proto::UserReq& req, tbox::proto::UserRes* res) {
    grpc::ClientContext context;
    auto status = stub_->UserOp(&context, req, res);
    if (!status.ok()) {
      LOG(ERROR) << "Grpc error: " << context.debug_error_string();
      return false;
    }
    if (res->err_code()) {
      LOG(ERROR) << "Server error: " << res->err_code();
      return false;
    }
    return true;
  }

  bool UserRegister() {
    tbox::proto::UserReq req;
    tbox::proto::UserRes res;
    req.set_request_id(tbox::util::Util::UUID());
    req.set_op(tbox::proto::UserOp::UserLogin);
    req.set_user(ConfigManager::Instance()->User());
    req.set_password(tbox::util::Util::SHA256("admin"));
    if (!DoRpc(req, &res)) {
      LOG(ERROR) << "Login error";
      return false;
    }

    LOG(INFO) << "Login success, token: " << res.token();

    req.Clear();
    res.Clear();
    req.set_request_id(tbox::util::Util::UUID());
    req.set_op(tbox::proto::UserOp::UserChangePassword);
    req.set_user(ConfigManager::Instance()->User());
    req.set_password(ConfigManager::Instance()->Password());
    req.set_token(res.token());
    if (!DoRpc(req, &res)) {
      LOG(ERROR) << "Change password error";
      return false;
    }

    LOG(INFO) << "Change password success, token: " << res.token();
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

  if (argc != 2) {
    LOG(ERROR) << "must have op arg";
    return -1;
  }

  std::string home_dir = tbox::util::Util::HomeDir();
  tbox::util::ConfigManager::Instance()->Init(home_dir +
                                              "/conf/client_base_config.json");
  GrpcClient grpc_client(
      ConfigManager::Instance()->ServerAddr(),
      std::to_string(ConfigManager::Instance()->GrpcServerPort()));
  if (FLAGS_op == "register") {
    auto ret = grpc_client.UserRegister();
    if (ret) {
      LOG(INFO) << "Register user success";
    } else {
      LOG(INFO) << "Register user error";
    }
  } else if (FLAGS_op == "changepw") {
  }
  return 0;
}
