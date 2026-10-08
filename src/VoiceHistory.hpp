#pragma once

#include "VoiceSession.hpp"
#include <algorithm>
#include <chrono>
#include <mutex>

namespace Voice {
// Only dialogue text is stored. Audio, screenshots, tool payloads and credentials
// never enter this local history. Streaming updates stay in memory until finish.
class History {
 public:
  static constexpr size_t Limit = 200;
  using Save = std::function<void(const nlohmann::json&)>;

  explicit History(const nlohmann::json& saved = nlohmann::json::array(), Save save = {})
      : save_(std::move(save)) {
    if (!saved.is_array()) return;
    for (const auto& item : saved) {
      if (!item.is_object() || !item.contains("id") || !item["id"].is_number_integer() ||
          !item.contains("created_at") || !item["created_at"].is_number_integer() ||
          !item.contains("user") || !item["user"].is_string() ||
          !item.contains("assistant") || !item["assistant"].is_string() ||
          !item.contains("state") || !item["state"].is_string()) continue;
      if (item["id"].is_number_integer() && !item["id"].is_number_unsigned() && item["id"].get<int64_t>() <= 0) continue;
      const auto id = item["id"].get<uint64_t>();
      const auto user = item["user"].get<std::string>();
      const auto assistant = item["assistant"].get<std::string>();
      auto state = item["state"].get<std::string>();
      if (id == 0 || id <= nextId_ || user.size() > 16000 || assistant.size() > 16000 ||
          (state != "pending" && state != "completed" && state != "interrupted" && state != "failed")) continue;
      if (state == "pending") state = "interrupted";
      entries_.push_back({{"id", id}, {"created_at", item["created_at"]},
          {"user", user}, {"assistant", assistant}, {"state", state}});
      nextId_ = id;
      if (entries_.size() > Limit) entries_.erase(entries_.begin());
    }
  }

  void Apply(const ConversationEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (event.type == ConversationEvent::Type::Started) {
      const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch()).count();
      entries_.push_back({{"id", ++nextId_}, {"created_at", timestamp}, {"user", ""},
          {"assistant", ""}, {"state", "pending"}});
      turns_[event.turn] = nextId_;
      if (entries_.size() > Limit) {
        const auto removed = entries_.front()["id"].get<uint64_t>();
        entries_.erase(entries_.begin());
        for (auto it = turns_.begin(); it != turns_.end();) {
          if (it->second <= removed) it = turns_.erase(it);
          else ++it;
        }
      }
    } else {
      const auto turn = turns_.find(event.turn);
      if (turn == turns_.end()) return;
      auto item = std::find_if(entries_.begin(), entries_.end(), [&](const auto& value) {
        return value["id"] == turn->second;
      });
      if (item == entries_.end()) return;
      switch (event.type) {
        case ConversationEvent::Type::UserTranscript:
          if (event.text.size() <= 16000) (*item)["user"] = event.text;
          break;
        case ConversationEvent::Type::AssistantTranscript:
          if ((*item)["state"] == "pending" && event.text.size() <= 16000) (*item)["assistant"] = event.text;
          return;
        case ConversationEvent::Type::Completed: (*item)["state"] = "completed"; break;
        case ConversationEvent::Type::Interrupted: (*item)["state"] = "interrupted"; break;
        case ConversationEvent::Type::Failed: (*item)["state"] = "failed"; break;
        default: break;
      }
    }
    if (save_) {
      try { save_(entries_); saveFailed_ = false; }
      catch (const std::exception&) { saveFailed_ = true; }
    }
  }

  nlohmann::json Snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto list = nlohmann::json::array();
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) list.push_back(*it);
    return {{"list", std::move(list)}, {"limit", Limit},
        {"error", saveFailed_ ? "对话记录暂时无法保存，重启后可能丢失。" : ""}};
  }

 private:
  std::mutex mutex_;
  nlohmann::json entries_ = nlohmann::json::array();
  std::map<uint64_t, uint64_t> turns_;
  uint64_t nextId_ = 0;
  Save save_;
  bool saveFailed_ = false;
};
}  // namespace Voice
