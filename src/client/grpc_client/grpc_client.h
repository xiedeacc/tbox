/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_CLIENT_GRPC_CLIENT_GRPC_CLIENT_H
#define TBOX_CLIENT_GRPC_CLIENT_GRPC_CLIENT_H

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

#include "glog/logging.h"
#include "grpcpp/client_context.h"
#include "grpcpp/grpcpp.h"
#include "grpcpp/support/client_callback.h"
#include "src/proto/service.grpc.pb.h"
#include "src/proto/service.pb.h"
#include "src/util/config_manager.h"
#include "src/util/thread_pool.h"
#include "src/util/util.h"

namespace tbox {
namespace client {

using tbox::util::ConfigManager;
class GrpcClient {
 public:
  explicit GrpcClient(const std::string& addr, const std::string& port)
      : channel_(grpc::CreateChannel(addr + ":" + port,
                                     grpc::InsecureChannelCredentials())),
        stub_(tbox::proto::TboxService::NewStub(channel_)) {}

  bool Init() {
    int try_times = 0;
    while (try_times < 5) {
      if (!Login()) {
        ++try_times;
        continue;
      }
      break;
    }

    if (try_times >= 5) {
      LOG(ERROR) << "Login too many tries";
      return false;
    }
    return true;
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
    tbox::proto::UserReq req;
    tbox::proto::UserRes res;
    req.set_request_id(tbox::util::Util::UUID());
    req.set_op(tbox::proto::UserOp::UserLogin);
    req.set_user(ConfigManager::Instance()->User());
    req.set_password(ConfigManager::Instance()->Password());
    if (!DoRpc(req, &res)) {
      LOG(ERROR) << "Login error";
      return false;
    }
    token_ = res.token();
    LOG(INFO) << "Login success, token: " << token_;
    return true;
  }

  void FillReq(proto::ServerReq* req) {
    req->Clear();
    req->set_request_id(util::Util::UUID());
    req->set_user(util::ConfigManager::Instance()->User());
    req->set_token(token_);
    req->set_op(proto::ServerOp::ServerUpdateDevIp);
    std::vector<folly::IPAddress> ip_addrs;
    util::Util::ListAllIPAddresses(&ip_addrs);
    for (const auto& addr : ip_addrs) {
      if (addr.isV4()) {
        if (!addr.isPrivate()) {
          req->mutable_context()->add_public_ipv4(addr.str());
        } else {
          req->mutable_context()->add_private_ipv4(addr.str());
        }
      } else if (addr.isV6()) {
        if (!addr.isPrivate()) {
          req->mutable_context()->add_public_ipv6(addr.str());
        } else {
          req->mutable_context()->add_private_ipv6(addr.str());
        }
      }
    }
  }

  void UpdateIPAddrs() {
    proto::ServerReq req;
    proto::ServerRes res;
    while (!stop_) {
      grpc::ClientContext context;
      FillReq(&req);
      if (!DoRpc(req, &res)) {
        LOG(ERROR) << "Grpc error";
      } else {
        LOG(INFO) << "UpdateIPAddrs success";
      }
      std::unique_lock<std::mutex> locker(mu_);
      cv_.wait_for(locker, std::chrono::seconds(5), [this] { return stop_; });
    }

    thread_exists_ = true;
    LOG(INFO) << "UpdateIPAddrs exist";
  }

  void Shutdown() {
    stop_ = true;
    cv_.notify_all();
  }

  void Start() {
    auto task = std::bind(&GrpcClient::UpdateIPAddrs, this);
    util::ThreadPool::Instance()->Post(task);
  }

  void Await() {
    {
      std::unique_lock<std::mutex> locker(mu_);
      cv_.wait(locker, [this] { return stop_; });
    }
    while (!thread_exists_) {
      cv_.notify_all();
      util::Util::Sleep(200);
    }
  }

 private:
  std::shared_ptr<grpc::Channel> channel_;
  std::unique_ptr<tbox::proto::TboxService::Stub> stub_;
  volatile bool stop_ = false;
  volatile bool thread_exists_ = false;
  std::mutex mu_;
  std::condition_variable cv_;
  std::string token_;
  tbox::proto::UserReq req_;
  tbox::proto::UserRes res_;
};

}  // namespace client
}  // namespace tbox

#endif  // TBOX_CLIENT_GRPC_CLIENT_GRPC_CLIENT_H
