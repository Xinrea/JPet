#pragma once

#include "VoicePlatform.hpp"
#include <atomic>
#include <mutex>
#include <utility>

namespace Voice {
struct EventQueue {
  std::atomic<uint64_t> connection{0};
  std::atomic<uint64_t> capture{0};
  std::atomic<bool> capturing{false};
  std::mutex mutex;
  std::vector<Event> events;
  size_t bytes = 0;

  void Push(Event::Type type, std::string data) {
    std::lock_guard<std::mutex> lock(mutex);
    PushLocked(type, std::move(data));
  }

  void PushLocked(Event::Type type, std::string data) {
    // A stalled render loop must not accumulate unbounded audio/network data.
    if (bytes + data.size() > 8 * 1024 * 1024) {
      events.clear();
      data = "语音处理暂时跟不上，请按快捷键重新开启麦克风";
      type = Event::Type::Error;
      bytes = 0;
    }
    bytes += data.size();
    events.push_back({type, std::move(data)});
  }

  void Network(uint64_t generation, Event::Type type, std::string data) {
    std::lock_guard<std::mutex> lock(mutex);
    if (connection == generation) PushLocked(type, std::move(data));
  }

  void Captured(uint64_t generation, Event::Type type, std::string data) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!capturing || capture != generation) return;
    if (type == Event::Type::Error) capturing = false;
    PushLocked(type, std::move(data));
  }

  std::vector<Event> Poll() {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<Event> result;
    result.swap(events);
    bytes = 0;
    return result;
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(mutex);
    events.clear();
    bytes = 0;
  }
};
}  // namespace Voice
