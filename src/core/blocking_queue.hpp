#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <utility>

template <typename T>
class BlockingQueue {
 public:
  bool Push(T item) {
    std::lock_guard<std::mutex> lock(mu_);
    if (closed_) {
      return false;
    }
    q_.push_back(std::move(item));
    cv_.notify_one();
    return true;
  }

  bool Pop(T& out) {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock, [&] { return closed_ || !q_.empty(); });
    if (q_.empty()) {
      return false;
    }
    out = std::move(q_.front());
    q_.pop_front();
    return true;
  }

  void Close() {
    std::lock_guard<std::mutex> lock(mu_);
    closed_ = true;
    cv_.notify_all();
  }

 private:
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<T> q_;
  bool closed_ = false;
};
