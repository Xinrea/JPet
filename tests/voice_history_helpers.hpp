#pragma once
#include "VoiceHistory.hpp"

// Project message history into turns only for existing protocol assertions.
// Message ordering, migration and tool summaries are asserted independently.
inline nlohmann::json TurnSnapshot(Voice::History& history) {
  using nlohmann::json;
  std::map<uint64_t, json> turns;
  const auto messages = history.Snapshot()["list"];
  for (const auto& item : messages) {
    const auto id = item["turn_id"].get<uint64_t>();
    if (!turns.count(id)) turns[id] = {{"id", id}, {"created_at", item["created_at"]}, {"user", ""}, {"assistant", ""}, {"state", item["state"]}};
    if (item["role"] == "user") { turns[id]["user"] = item["text"]; turns[id]["state"] = item["state"]; }
    if (item["role"] == "assistant") {
      auto& text = turns[id]["assistant"].get_ref<std::string&>();
      if (!text.empty()) text += "\n\n";
      text += item["text"].get<std::string>();
    }
  }
  auto list = json::array();
  for (auto it = turns.rbegin(); it != turns.rend(); ++it) list.push_back(it->second);
  return list;
}
