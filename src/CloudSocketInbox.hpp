#pragma once

#include "CloudSocket.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>

class CloudSocketInbox {
 public:
  static constexpr size_t MaxMessageBytes = 4 * 1024 * 1024;
  explicit CloudSocketInbox(std::function<void()> notify) : notify_(std::move(notify)) {}
  void Push(CloudSocketEvent event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) return;
    if (event.text.size() > MaxMessageBytes || bytes_ + event.text.size() > 16 * 1024 * 1024) {
      events_.clear(); bytes_ = 0;
      event = {CloudSocketEvent::Type::Error, "云端消息过大，正在重新连接"};
    }
    if (event.type == CloudSocketEvent::Type::Error) closed_ = true;
    bytes_ += event.text.size();
    events_.push_back(std::move(event));
    changed_.notify_all();
    notify_();
  }
  bool Receive(CloudSocketEvent& event, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    changed_.wait_for(lock, timeout, [&] { return closed_ || !events_.empty(); });
    if (events_.empty()) return false;
    event = std::move(events_.front()); events_.pop_front();
    bytes_ -= event.text.size();
    return true;
  }
  void Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true; events_.clear(); bytes_ = 0;
    changed_.notify_all();
  }
 private:
  std::mutex mutex_;
  std::condition_variable changed_;
  std::deque<CloudSocketEvent> events_;
  size_t bytes_ = 0;
  bool closed_ = false;
  std::function<void()> notify_;
};
