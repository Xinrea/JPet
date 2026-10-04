#include "DataManager.hpp"
#include "CloudGame.hpp"

nlohmann::json DataManager::GetAchievementState() {
  auto state = GetCloudSnapshot();
  if (!state.is_null()) return state["achievements"];
  if (!GetWithDefault("uid", std::string{}).empty() && !GetWithDefault("cloud.migration_uid", std::string{}).empty()) return Achievements::Describe(Achievements::Parse(""));
  return Achievements::Describe(Achievements::Parse(GetWithDefault("achievements.state", std::string{})));
}
void DataManager::RecordAchievementEvent(const std::string& event, time_t) {
  if (event == "touch") CloudGame::GetInstance()->Touch();
}
