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

#include "aws/route53/model/RRType.h"
#include "curl/curl.h"
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

  ~GrpcClient() {
    if (curl_) {
      curl_easy_cleanup(curl_);
      curl_ = nullptr;
    }
  }

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
    curl_ = curl_easy_init();
    if (curl_) {
      LOG(INFO) << "Init curl success";
    } else {
      LOG(INFO) << "Init curl error";
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
      LOG(ERROR) << req.request_id() << " Grpc error";
      return false;
    }
    if (res->err_code()) {
      LOG(ERROR) << req.request_id() << " Server error: " << res->err_code();
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
      if (addr.isLoopback()) {
        continue;
      }
      if (addr.isV4()) {
        if (!addr.isPrivate()) {
          req->mutable_context()->add_public_ipv4(addr.str());
        } else {
          req->mutable_context()->add_private_ipv4(addr.str());
        }
      } else if (addr.isV6()) {
        if (!addr.isLinkLocal()) {
          req->mutable_context()->add_public_ipv6(addr.str());
        } else {
          req->mutable_context()->add_private_ipv6(addr.str());
        }
      }
    }
    req->mutable_context()->set_outer_ipv4(ipv4_);
    req->mutable_context()->set_outer_ipv6(ipv6_);

    // std::string json;
    // util::Util::MessageToJson(*req, &json, true);
    // LOG(INFO) << json;
  }

  static size_t WriteCallback(void* contents, size_t size, size_t nmemb,
                              void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
  }

  bool GetOuterIP() {
    std::string read_buf;
    bool update_dns = false;
    curl_easy_setopt(curl_, CURLOPT_URL, "https://ip.xiedeacc.com");
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, &GrpcClient::WriteCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &read_buf);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl_, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    auto ret = curl_easy_perform(curl_);
    if (ret != CURLE_OK) {
      LOG(INFO) << "failed: " << curl_easy_strerror(ret);
    } else {
      util::Util::Trim(&read_buf);
      if (!read_buf.empty() && ipv4_ != read_buf) {
        update_dns = true;
        ipv4_ = read_buf;
      }
      LOG(INFO) << "result: " << read_buf;
    }

    read_buf.clear();
    curl_easy_setopt(curl_, CURLOPT_URL, "https://ip.xiedeacc.com");
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, &GrpcClient::WriteCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &read_buf);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl_, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6);
    ret = curl_easy_perform(curl_);
    if (ret != CURLE_OK) {
      LOG(INFO) << "failed: " << curl_easy_strerror(ret);
    } else {
      util::Util::Trim(&read_buf);
      if (!read_buf.empty() && ipv6_ != read_buf) {
        update_dns = true;
        ipv6_ = read_buf;
      }
      LOG(INFO) << "result: " << read_buf;
    }
    return update_dns;
  }

  void UpdateDns(proto::ServerReq& req, proto::ServerRes* res) {
    auto update_dns = GetOuterIP();
    if (update_dns) {
      req.set_op(proto::ServerUpdateDevDns);
      req.mutable_context()->set_outer_ipv4(ipv4_);
      req.mutable_context()->set_outer_ipv6(ipv6_);
      req.set_hosted_zone_id(util::ConfigManager::Instance()->HostedZoneId());
      req.set_record_name("dev.xiedeacc.com");
      req.set_record_value(ipv4_);
      req.set_record_type((int32_t)Aws::Route53::Model::RRType::A);
      if (!DoRpc(req, res)) {
        LOG(ERROR) << req.request_id() << " Update dns type A error";
      } else {
        LOG(INFO) << req.request_id() << " Update dns type A success";
      }

      req.set_record_value(ipv6_);
      req.set_record_type((int32_t)Aws::Route53::Model::RRType::AAAA);
      if (!DoRpc(req, res)) {
        LOG(ERROR) << req.request_id() << " UpdateDns type AAAA error";
      } else {
        LOG(INFO) << req.request_id() << " UpdateDns type AAAA success";
      }
      std::string json;
      util::Util::MessageToJson(req, &json, true);
      LOG(INFO) << json;
    }
  }

  void UpdateIp(proto::ServerReq& req, proto::ServerRes* res) {
    if (!DoRpc(req, res)) {
      LOG(ERROR) << req.request_id() << " UpdateIp error";
    } else {
      LOG(INFO) << req.request_id() << " UpdateIp success";
    }
  }

  void UpdateDev() {
    proto::ServerReq req;
    proto::ServerRes res;
    while (!stop_) {
      grpc::ClientContext context;
      FillReq(&req);
      UpdateDns(req, &res);
      req.set_request_id(util::Util::UUID());
      UpdateIp(req, &res);
      std::unique_lock<std::mutex> locker(mu_);
      cv_.wait_for(locker, std::chrono::seconds(5), [this] { return stop_; });
    }

    thread_exists_ = true;
    LOG(INFO) << "UpdateDev exist";
  }

  void Shutdown() {
    stop_ = true;
    cv_.notify_all();
  }

  void Start() {
    auto task = std::bind(&GrpcClient::UpdateDev, this);
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

  std::string ipv4_;
  std::string ipv6_;
  CURL* curl_ = nullptr;
};

}  // namespace client
}  // namespace tbox

#endif  // TBOX_CLIENT_GRPC_CLIENT_GRPC_CLIENT_H
