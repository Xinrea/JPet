#include "DataManager.hpp"
#include "PanelServer.hpp"
#include <ctime>

void DataManager::LoadAchievements() {
  if (achievementsLoaded) return;
  const auto saved = GetWithDefault("achievements.state", std::string{});
  achievementState = Achievements::Parse(saved);
  if (saved.empty()) {
    // Only migrate evidence that still exists. The old save did not retain
    // lifetime counters, so don't guess lost completions or companion time.
    auto history = nlohmann::json::parse(GetWithDefault("task.history", std::string{"[]"}), nullptr, false);
    if (history.is_array()) {
      for (const auto& item : history) {
        if (!item.is_object() || !item.contains("id") || !item["id"].is_number_integer() ||
            !item.contains("success") || !item["success"].is_boolean()) continue;
        int id = item["id"].get<int>();
        if (id < 1 || id > 13) continue;
        if (item["success"] == true) {
          Achievements::Increment(achievementState, "successes");
          Achievements::Increment(achievementState, "task." + std::to_string(id));
        } else Achievements::Increment(achievementState, "failures");
      }
    }
    for (int id = 1; id <= 13; ++id) {
      auto prefix = "task." + std::to_string(id) + ".";
      auto status = GetWithDefault(prefix + "status", 0);
      bool completed = status == static_cast<int>(TStatus::ARCHIVED) ||
          (status == static_cast<int>(TStatus::IDLE) && GetWithDefault(prefix + "end_time", 0) > 0 &&
           GetWithDefault(prefix + "success", 0) == 1);
      if (completed && Achievements::Number(achievementState["metrics"], "task." + std::to_string(id)) == 0) {
        Achievements::Increment(achievementState, "successes");
        Achievements::Increment(achievementState, "task." + std::to_string(id));
      }
    }
  }
  achievementsLoaded = true;
}

std::map<std::string, int> DataManager::AchievementSnapshot(const std::map<std::string, int>& updates) {
  auto read = [&](const std::string& key) {
    auto it = updates.find(key);
    return it == updates.end() ? GetWithDefault(key, 0) : it->second;
  };
  std::map<std::string, int> result;
  int balanced = 99999999;
  for (auto key : {"speed", "endurance", "strength", "will", "intellect"}) {
    result[key] = read(std::string{"attr."} + key);
    balanced = std::min(balanced, result[key]);
  }
  result["balanced"] = balanced;
  result["exp"] = read("attr.exp");
  result["stars"] = read("starcnt");
  result["dress"] = read("clothes.1.active") == 1;
  result["winter"] = read("clothes.2.active") == 1;
  result["clothes"] = 1 + result["dress"] + result["winter"];
  return result;
}

void DataManager::NotifyAchievements(const nlohmann::json& unlocked) {
  if (!unlocked.empty()) PanelServer::GetInstance()->Notify(nlohmann::json{
      {"type", "ACHIEVEMENT_UNLOCKED"}, {"achievements", unlocked}}.dump());
}

void DataManager::RefreshAchievements(time_t now, bool notify) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  LoadAchievements();
  auto next = achievementState;
  Achievements::Observe(next, AchievementSnapshot());
  auto unlocked = Achievements::Evaluate(next, now);
  if (next != achievementState || GetWithDefault("achievements.state", std::string{}).empty()) {
    gameData->UpdateBatch({}, {{"achievements.state", next.dump()}});
    achievementState = next;
  }
  if (notify) NotifyAchievements(unlocked);
}

void DataManager::RecordAchievementEvent(const std::string& event, time_t now) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  LoadAchievements();
  auto next = achievementState;
  if (event == "minute") {
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char date[11];
    std::strftime(date, sizeof(date), "%Y-%m-%d", &local);
    Achievements::Minute(next, date);
  } else if (event == "touch") Achievements::Increment(next, "touches");
  else return;
  Achievements::Observe(next, AchievementSnapshot());
  auto unlocked = Achievements::Evaluate(next, now);
  gameData->UpdateBatch({}, {{"achievements.state", next.dump()}});
  achievementState = next;
  NotifyAchievements(unlocked);
}

nlohmann::json DataManager::GetAchievementState() {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  RefreshAchievements();
  return Achievements::Describe(achievementState);
}
