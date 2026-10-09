#pragma once

#include <nlohmann/json.hpp>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>

namespace Voice {
// The tool worker waits here while the render thread applies window settings.
// Cancel/Stop must release pending requests before the worker is joined.
class SettingsQueue {
 public:
  nlohmann::json Invoke(std::function<nlohmann::json()> apply, std::function<bool()> cancelled = {}) {
    auto request = std::make_shared<Request>();
    request->apply = std::move(apply);
    request->cancelled = std::move(cancelled);
    auto result = request->result.get_future();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopped_) return Cancelled();
      requests_.push_back(request);
    }
    return result.get();
  }
  void Drain() {
    std::deque<std::shared_ptr<Request>> requests;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      requests.swap(requests_);
    }
    for (const auto& request : requests) {
      try { request->result.set_value(request->cancelled && request->cancelled() ? Cancelled() : request->apply()); }
      catch (...) { request->result.set_exception(std::current_exception()); }
    }
  }
  void Cancel(bool stop = false) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop) stopped_ = true;
    for (const auto& request : requests_) request->result.set_value(Cancelled());
    requests_.clear();
  }
  void Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = false;
  }
 private:
  static nlohmann::json Cancelled() { return {{"ok", false}, {"error", "设置操作已取消，未执行"}}; }
  struct Request {
    std::function<nlohmann::json()> apply;
    std::function<bool()> cancelled;
    std::promise<nlohmann::json> result;
  };
  std::mutex mutex_;
  std::deque<std::shared_ptr<Request>> requests_;
  bool stopped_ = false;
};
} // namespace Voice
