/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_UTIL_THREAD_POOL_H
#define TBOX_UTIL_THREAD_POOL_H

#include <functional>
#include <future>
#include <memory>
#include <utility>

#include "boost/asio/post.hpp"
#include "boost/asio/thread_pool.hpp"
#include "src/proto/error.pb.h"
#include "src/util/config_manager.h"

namespace tbox {
namespace util {

class ThreadPool final {
 private:
  friend class folly::Singleton<ThreadPool>;
  ThreadPool() : terminated(false) {}

 public:
  static std::shared_ptr<ThreadPool> Instance();

  ~ThreadPool() {
    if (!terminated.load()) {
      pool_->stop();
      pool_->join();
      terminated.store(true);
    }
  }

  bool Init() {
    auto thread_num = ConfigManager::Instance()->EventThreads();
    LOG(INFO) << "thread pool size: " << thread_num;
    pool_ = std::make_shared<boost::asio::thread_pool>(thread_num);
    return true;
  }

  void Stop() {
    if (pool_) {
      pool_->stop();
      pool_->join();
      terminated.store(true);
    }
  }

  void Post(std::packaged_task<bool()>& task) {  // NOLINT
    boost::asio::post(*pool_.get(), std::move(task));
  }

  void Post(std::packaged_task<void()>& task) {  // NOLINT
    boost::asio::post(*pool_.get(), std::move(task));
  }

  void Post(std::packaged_task<proto::ErrCode()>& task) {  // NOLINT
    boost::asio::post(*pool_.get(), std::move(task));
  }

  void Post(std::function<void()> task) {
    boost::asio::post(*pool_.get(), std::move(task));
  }

  void Post(std::packaged_task<int()>& task) {  // NOLINT
    boost::asio::post(*pool_.get(), std::move(task));
  }

 private:
  std::shared_ptr<boost::asio::thread_pool> pool_;
  std::atomic_bool terminated;
};

}  // namespace util
}  // namespace tbox

#endif  // TBOX_UTIL_THREAD_POOL_H
