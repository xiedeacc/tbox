#ifndef TBOX_COMMON_BLOCKING_QUEUE_H
#define TBOX_COMMON_BLOCKING_QUEUE_H

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>

namespace tbox {
namespace common {

template <typename T>
class BlockingQueue {
 public:
  BlockingQueue() = default;

  size_t Size() {
    std::unique_lock<std::mutex> lock(mu_);
    return queue_.size();
  }

  void Await() {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock);
  }

  void PushBack(const T& t) {
    std::unique_lock<std::mutex> lock(mu_);
    queue_.emplace_back(t);
    cv_.notify_all();
  }

  bool PopBack(T* t) {
    std::unique_lock<std::mutex> lock(mu_);
    if (queue_.empty()) {
      return false;
    }

    *t = std::move(queue_.back());
    queue_.pop_back();
    return true;
  }

  bool PopFront(T* t) {
    std::unique_lock<std::mutex> lock(mu_);
    if (queue_.empty()) {
      return false;
    }

    *t = queue_.front();
    queue_.pop_front();
    return true;
  }

  void Clear() {
    std::unique_lock<std::mutex> lock(mu_);
    queue_.clear();
  }

 private:
  std::deque<T> queue_;
  std::mutex mu_;
  std::condition_variable cv_;
};

}  // namespace common
}  // namespace tbox

#endif  // TBOX_COMMON_BLOCKING_QUEUE_H
