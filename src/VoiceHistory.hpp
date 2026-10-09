#pragma once

#include "VoiceSession.hpp"
#include <algorithm>
#include <chrono>
#include <mutex>

namespace Voice {
// Ordered messages with bounded tool summaries. Never retain audio, screenshots,
// raw tool results or credential fields. Streamed text is saved at turn finish.
class History {
 public:
  static constexpr size_t Limit = 200;
  using json = nlohmann::json;
  using Save = std::function<void(const json&)>;

  explicit History(const json& saved = json::array(), Save save = {}) : save_(std::move(save)) {
    if (!saved.is_array()) return;
    for (const auto& item : saved) {
      if (!item.is_object() || !PositiveId(item, "id") || !item.contains("created_at") ||
          !item["created_at"].is_number_integer() || !item.contains("state") || !item["state"].is_string()) continue;
      auto state = item["state"].get<std::string>();
      if (!ValidState(state)) continue;
      if (state == "pending") state = "interrupted";
      // Migrate legacy question/answer pairs into independent messages.
      if (item.contains("user") && item["user"].is_string() && item.contains("assistant") && item["assistant"].is_string()) {
        const auto turn = ++nextTurn_;
        for (const auto* role : {"user", "assistant"}) {
          const auto text = item[role].get<std::string>();
          if (text.size() > 16000 || (text.empty() && std::string(role) == "assistant")) continue;
          Add(turn, role, text, state, item["created_at"]);
        }
      } else if (item.contains("role") && item["role"].is_string() && item.contains("text") && item["text"].is_string() && PositiveId(item, "turn_id")) {
        const auto role = item["role"].get<std::string>();
        const auto id = item["id"].get<uint64_t>(), turn = item["turn_id"].get<uint64_t>();
        if ((role != "user" && role != "assistant" && role != "tool") || id <= nextId_ || item["text"].get_ref<const std::string&>().size() > 16000) continue;
        auto message = json{{"id", id}, {"turn_id", turn}, {"created_at", item["created_at"]}, {"role", role}, {"text", item["text"]}, {"state", state}};
        if (role == "tool") {
          message["name"] = KnownTool(item.contains("name") && item["name"].is_string() ? item["name"].get<std::string>() : "");
          message["text"] = "";
          message["arguments"] = Arguments(item.value("arguments", json::object()));
          if (item.contains("result")) message["result"] = Result(item["result"]);
        }
        entries_.push_back(std::move(message)); nextId_ = id; nextTurn_ = std::max(nextTurn_, turn);
      }
      TrimEntries();
    }
  }

  void Apply(const ConversationEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    using Type = ConversationEvent::Type;
    if (event.type == Type::Started) {
      const auto turn = ++nextTurn_;
      turns_[event.turn] = turn;
      Add(turn, "user", "", "pending");
    } else {
      const auto found = turns_.find(event.turn);
      if (found == turns_.end()) return;
      const auto turn = found->second;
      if (event.type == Type::Completed || event.type == Type::Interrupted || event.type == Type::Failed) {
        const char* state = event.type == Type::Completed ? "completed" : event.type == Type::Failed ? "failed" : "interrupted";
        for (auto& item : entries_) if (item["turn_id"] == turn && item["state"] == "pending") item["state"] = state;
      } else {
        const auto role = event.type == Type::UserTranscript || event.type == Type::InputFailed ? "user" : event.type == Type::AssistantTranscript ? "assistant" : "tool";
        auto item = std::find_if(entries_.begin(), entries_.end(), [&](const auto& value) {
          const auto key = keys_.find(value["id"].template get<uint64_t>());
          return value["turn_id"] == turn && value["role"] == role && (std::string(role) == "user" || (key != keys_.end() && key->second == event.id));
        });
        if (item == entries_.end()) {
          if (event.type != Type::AssistantTranscript && event.type != Type::ToolStarted) return;
          Add(turn, role, "", "pending"); keys_[nextId_] = event.id; item = std::prev(entries_.end());
        }
        if (event.type == Type::AssistantTranscript || event.type == Type::UserTranscript) {
          if (event.text.size() <= 16000 && (event.type == Type::UserTranscript || (*item)["state"] == "pending")) (*item)["text"] = event.text;
          if (event.type == Type::AssistantTranscript) { TrimEntries(); return; }
        } else if (event.type == Type::InputFailed) (*item)["state"] = "failed";
        else if (event.type == Type::ToolStarted) {
          (*item)["name"] = KnownTool(event.text); (*item)["arguments"] = Arguments(event.data);
        } else if (event.type == Type::ToolCompleted) {
          (*item)["result"] = Result(event.data);
          (*item)["state"] = event.data.value("interrupted", false) ? "interrupted" : event.data.value("ok", false) ? "completed" : "failed";
        }
      }
    }
    TrimEntries(); SaveEntries();
  }

  json Snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    return {{"list", entries_}, {"limit", Limit}, {"error", saveFailed_ ? "对话记录暂时无法保存，重启后可能丢失。" : ""}};
  }

 private:
  static bool PositiveId(const json& item, const char* key) {
    return item.contains(key) && item[key].is_number_integer() &&
        (item[key].is_number_unsigned() ? item[key].get<uint64_t>() > 0 : item[key].get<int64_t>() > 0);
  }
  static bool ValidState(const std::string& state) {
    return state == "pending" || state == "completed" || state == "interrupted" || state == "failed";
  }
  static std::string KnownTool(const std::string& name) {
    for (const auto* tool : {"view_desktop", "get_game_profile", "get_game_clothes", "get_task_catalog", "get_current_task", "get_task_queue", "get_task_history", "get_game_achievements", "get_game_statistics", "get_game_rank", "game_action", "jpet_settings", "web_search", "bilibili_search", "open_url"}) if (name == tool) return name;
    return "unknown";
  }
  static json Select(const json& data, std::initializer_list<const char*> fields) {
    auto summary = json::object();
    if (!data.is_object()) return summary;
    for (const auto* field : fields) {
      if (!data.contains(field)) continue;
      const auto& value = data[field];
      if (value.is_boolean() || value.is_number() || (value.is_string() && value.get_ref<const std::string&>().size() <= 1500)) summary[field] = value;
    }
    return summary;
  }
  static json Settings(const json& data) {
    return Select(data, {"volume", "scale", "mute", "idle_audio", "touch_audio", "green", "limit", "track", "dropfile", "dynamic", "live", "update", "long_hair", "left_ear", "right_ear", "hat", "glasses", "star_eyes", "dizzy_eyes", "sweat", "dark_face", "blush", "leg_accessories", "shoes", "tail", "gun", "mouth"});
  }
  static json Arguments(const json& data) {
    auto summary = Select(data, {"action", "section", "task_id", "entry_id", "direction", "attribute", "clothes_id", "offset", "limit", "metric", "question", "display", "query", "type", "order", "page", "url", "uid", "shortcut_type", "target"});
    if (!data.is_object()) summary["notice"] = "参数不是有效的 JSON 对象";
    else if (data.contains("settings")) summary["settings"] = Settings(data["settings"]);
    return Bounded(std::move(summary));
  }
  static json Result(const json& data) {
    auto summary = Select(data, {"ok", "error", "notice", "action", "section", "url", "code", "results_count", "sources_count"});
    if (data.is_object() && data.contains("settings") && data["settings"].is_object()) {
      auto settings = Settings(data["settings"]);
      for (const auto* section : {"audio", "display", "interaction", "notifications", "appearance"}) if (data["settings"].contains(section)) settings[section] = Settings(data["settings"][section]);
      if (!settings.empty()) summary["settings"] = std::move(settings);
    }
    if (data.is_object()) for (const auto* field : {"results", "sources"}) if (data.contains(field) && data[field].is_array()) summary[std::string(field) + "_count"] = data[field].size();
    return Bounded(std::move(summary));
  }
  static json Bounded(json summary) {
    // Corrupt saved data or invalid parameters must not grow history responses.
    if (summary.dump().size() <= 4096) return summary;
    auto compact = json::object();
    if (summary.contains("ok") && summary["ok"].is_boolean()) compact["ok"] = summary["ok"];
    for (const auto* key : {"action", "section", "code", "error"})
      if (summary.contains(key) && summary[key].is_string() && summary[key].get_ref<const std::string&>().size() <= (std::string(key) == "error" ? 1500 : 64)) compact[key] = summary[key];
    compact["notice"] = "摘要过长，部分字段已省略";
    return compact;
  }
  void Add(uint64_t turn, const std::string& role, const std::string& text, const std::string& state, json timestamp = nullptr) {
    if (timestamp.is_null()) timestamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    entries_.push_back({{"id", ++nextId_}, {"turn_id", turn}, {"created_at", timestamp}, {"role", role}, {"text", text}, {"state", state}});
  }
  void TrimEntries() {
    while (entries_.size() > Limit) { keys_.erase(entries_.front()["id"].get<uint64_t>()); entries_.erase(entries_.begin()); }
    for (auto it = turns_.begin(); it != turns_.end();) {
      if (std::none_of(entries_.begin(), entries_.end(), [&](const auto& item) { return item["turn_id"] == it->second; })) it = turns_.erase(it);
      else ++it;
    }
  }
  void SaveEntries() {
    if (!save_) return;
    try { save_(entries_); saveFailed_ = false; } catch (const std::exception&) { saveFailed_ = true; }
  }
  std::mutex mutex_;
  json entries_ = json::array();
  std::map<uint64_t, uint64_t> turns_;
  std::map<uint64_t, std::string> keys_;
  uint64_t nextId_ = 0, nextTurn_ = 0;
  Save save_;
  bool saveFailed_ = false;
};
}  // namespace Voice
