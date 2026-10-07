#include "DataManager.hpp"
#include "CloudGame.hpp"
#include "PanelServer.hpp"
#include "Platform.hpp"

namespace {
std::string CloudCacheKey(const std::string& endpoint, const std::string& uid) {
  return "cloud.cache." + uid + "." + endpoint;
}
}

nlohmann::json DataManager::ExportCloudBootstrap(const std::string& uid) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  const auto owner = GetWithDefault("cloud.migration_uid", std::string{});
  if (!uid.empty() && !owner.empty()) return nullptr;
  if (legacyBootstrap.is_object() && legacyBootstrap.contains("profile")) return legacyBootstrap;
  nlohmann::json profile = {{"attributes", nlohmann::json::object()},
    {"starcnt", GetWithDefault("starcnt", 0)},
    {"clothes", {{"current", GetWithDefault("clothes.current", 0)},
      {"unlock", {true, GetWithDefault("clothes.1.active", 0) == 1, GetWithDefault("clothes.2.active", 0) == 1}}}}};
  for (auto key : {"speed", "endurance", "strength", "will", "intellect", "exp", "buycnt"}) profile["attributes"][key] = GetAttribute(key);
  nlohmann::json result = {{"profile", profile}, {"tasks", nlohmann::json::array()},
    {"queue", nlohmann::json::array()}, {"failcount", GetWithDefault("buff.failcount", 0)}};
  auto saved = nlohmann::json::parse(GetWithDefault("achievements.state", std::string{}), nullptr, false);
  if (saved.is_object()) result["achievements"] = saved;
  saved = nlohmann::json::parse(GetWithDefault("task.queue", std::string{}), nullptr, false);
  if (saved.is_object() && saved.contains("entries")) result["queue"] = saved["entries"];
  saved = nlohmann::json::parse(GetWithDefault("task.history", std::string{}), nullptr, false);
  if (saved.is_array()) result["history"] = saved;
  const auto now = time(nullptr);
  for (int id = 1; id <= 13; ++id) {
    const auto prefix = "task." + std::to_string(id) + ".";
    const auto status = TaskStatus(id);
    result["tasks"].push_back({{"id", id}, {"status", status[3]}, {"success", status[2] == 1},
      {"end_time", status[1]}, {"cost_snapshot", status[4]},
      {"elapsed_seconds", status[3] == 1 ? std::max<int64_t>(0, now - status[0]) : 0},
      {"queued", GetWithDefault(prefix + "queued", 0) == 1}});
  }
  return result;
}

void DataManager::LoadCloudCache(const std::string& uid, const std::string& endpoint) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  cloudCacheUrl = endpoint.empty() ? CloudGame::ServiceUrl() : endpoint;
  auto cached = nlohmann::json::parse(GetWithDefault(CloudCacheKey(cloudCacheUrl, uid), std::string{}), nullptr, false);
  cloudSnapshot = cached.is_object() && cached.value("uid", std::string{}) == uid ? cached : nlohmann::json{};
  if (!cloudSnapshot.is_null()) cloudSnapshot["online"] = false;
  cloudReceivedAt = std::chrono::steady_clock::now();
}

bool DataManager::ApplyCloudSnapshot(const nlohmann::json& snapshot) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  if (snapshot.at("schema").get<int>() != 1) throw std::runtime_error("Unsupported cloud protocol");
  const auto uid = snapshot.at("uid").get<std::string>();
  if (uid != GetWithDefault("uid", std::string{})) return false;
  const auto revision = snapshot.at("revision").get<int64_t>();
  if (cloudSnapshot.is_object() && cloudSnapshot.value("uid", std::string{}) == uid && revision < cloudSnapshot.value("revision", int64_t{0})) return false;
  if (cloudSnapshot.is_object() && revision == cloudSnapshot.value("revision", int64_t{0}) &&
      snapshot.contains("server_time") && cloudSnapshot.contains("server_time") &&
      snapshot.at("server_time").get<int64_t>() <= cloudSnapshot.at("server_time").get<int64_t>()) return false;
  const auto& profile = snapshot.at("profile");
  const auto& clothes = profile.at("clothes");
  const auto& attributes = profile.at("attributes");
  std::map<std::string, int> integers;
  for (auto key : {"speed", "endurance", "strength", "will", "intellect", "exp", "buycnt"}) integers[std::string{"attr."} + key] = attributes.at(key).get<int>();
  integers["starcnt"] = profile.at("starcnt").get<int>();
  integers["clothes.current"] = clothes.at("current").get<int>();
  integers["clothes.1.active"] = clothes.at("unlock").at(1).get<bool>();
  integers["clothes.2.active"] = clothes.at("unlock").at(2).get<bool>();
  integers["buff.failcount"] = snapshot.at("save").at("failcount").get<int>();
  integers["data-share"] = snapshot.value("share", false);
  const auto previousClothes = GetWithDefault("clothes.current", 0);
  nlohmann::json unlocked = nlohmann::json::array();
  if (cloudSnapshot.is_object() && cloudSnapshot.contains("achievements")) {
    std::map<std::string, bool> before;
    for (const auto& item : cloudSnapshot["achievements"]["list"]) before[item.at("id").get<std::string>()] = item.value("unlocked", false);
    for (const auto& item : snapshot.at("achievements").at("list")) {
      if (item.value("unlocked", false) && !before[item.at("id").get<std::string>()]) unlocked.push_back({{"id", item["id"]}, {"title", item["title"]}, {"icon", item["icon"]}});
    }
  }
  bool taskCompleted = false;
  const auto& history = snapshot.at("tasks").at("history");
  if (cloudSnapshot.is_object() && cloudSnapshot.contains("tasks") && !history.empty()) {
    const auto& old = cloudSnapshot["tasks"]["history"];
    taskCompleted = old.empty() || old[0] != history[0];
  }
  gameData->UpdateBatch(integers, {{CloudCacheKey(cloudCacheUrl, uid), snapshot.dump()},
    {"cloud.migration_uid", GetWithDefault("cloud.migration_uid", std::string{}).empty() ? uid : GetWithDefault("cloud.migration_uid", std::string{})},
    {"achievements.state", snapshot.at("save").at("achievements").dump()}});
  cloudSnapshot = snapshot;
  cloudReceivedAt = std::chrono::steady_clock::now();
  if (previousClothes != integers["clothes.current"]) PostProcess("clothes.current", integers["clothes.current"]);
  if (!unlocked.empty()) PanelServer::GetInstance()->Notify(nlohmann::json{{"type", "ACHIEVEMENT_UNLOCKED"}, {"achievements", unlocked}}.dump());
  if (taskCompleted) {
    PanelServer::GetInstance()->Notify("TASK_COMPLETE");
#ifdef __APPLE__
    Platform::Notify(history[0].value("success", false) ? L"任务成功，奖励已发放" : L"任务失败，已自动结算",
      LAppPal::StringToWString(history[0].value("title", std::string{})), "TASK_COMPLETE");
#else
    GameTask notification;
    notification.Notify(history[0].value("success", false) ? L"任务成功，奖励已发放" : L"任务失败，已自动结算",
      LAppPal::StringToWString(history[0].value("title", std::string{})), new WinToastEventHandler("TASK_COMPLETE"));
#endif
  }
  PanelServer::GetInstance()->Notify("UPDATE");
  return true;
}

nlohmann::json DataManager::GetCloudSnapshot() {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  if (!cloudSnapshot.is_object() || !cloudSnapshot.contains("profile")) return nullptr;
  if (cloudCacheUrl != CloudGame::ServiceUrl()) return nullptr;
  const auto uid = GetWithDefault("uid", std::string{});
  if (!uid.empty() && cloudSnapshot.value("uid", std::string{}) != uid) return nullptr;
  auto result = cloudSnapshot;
  const auto delta = std::chrono::duration<double>(std::chrono::steady_clock::now() - cloudReceivedAt).count();
  const bool online = CloudGame::GetInstance()->Online() && cloudSnapshot.value("online", false) && delta * 1000 < cloudSnapshot.value("lease_remaining_ms", 0.0);
  result["online"] = online;
  // Interpolation is visual only; confirmed attributes and rewards are untouched.
  const auto elapsed = cloudSnapshot.value("online", false) ? std::min(delta, cloudSnapshot.value("lease_remaining_ms", 0.0) / 1000) : 0.0;
  result["profile"]["exp_progress_seconds"] = std::min(60.0, result["profile"].value("exp_progress_seconds", 0.0) + elapsed);
  if (result["tasks"].contains("current") && result["tasks"]["current"].is_object()) {
    auto& current = result["tasks"]["current"];
    const auto cost = current.value("cost", 0.0);
    current["elapsed_seconds"] = std::min(cost, current.value("elapsed_seconds", 0.0) + elapsed);
    current["remaining_seconds"] = std::max(0.0, cost - current["elapsed_seconds"].get<double>());
    current["paused"] = !online;
  }
  result["profile"]["online"] = online;
  result["profile"]["cloud"] = CloudGame::GetInstance()->Status();
  result["tasks"]["online"] = online;
  return result;
}

void DataManager::PauseCloudView() {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  if (!cloudSnapshot.is_object() || !cloudSnapshot.contains("profile") || !cloudSnapshot.value("online", false)) return;
  const auto delta = std::min(std::chrono::duration<double>(std::chrono::steady_clock::now() - cloudReceivedAt).count(),
    cloudSnapshot.value("lease_remaining_ms", 0.0) / 1000);
  cloudSnapshot["profile"]["exp_progress_seconds"] = std::min(60.0, cloudSnapshot["profile"].value("exp_progress_seconds", 0.0) + delta);
  auto& task = cloudSnapshot["tasks"]["current"];
  if (task.is_object()) {
    task["elapsed_seconds"] = std::min(task.value("cost", 0.0), task.value("elapsed_seconds", 0.0) + delta);
    task["remaining_seconds"] = std::max(0.0, task.value("cost", 0.0) - task["elapsed_seconds"].get<double>());
  }
  cloudSnapshot["online"] = false;
}

nlohmann::json DataManager::GetCloudProfile() {
  auto state = GetCloudSnapshot();
  if (!state.is_null()) return state["profile"];
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  const auto uid = GetWithDefault("uid", std::string{});
  const bool migrated = !GetWithDefault("cloud.migration_uid", std::string{}).empty();
  auto profile = migrated && !uid.empty() ? nlohmann::json{
    {"attributes", {{"speed", 0}, {"endurance", 0}, {"strength", 0}, {"will", 0}, {"intellect", 0}, {"exp", 0}, {"buycnt", 0}}},
    {"starcnt", 0}, {"clothes", {{"current", 0}, {"unlock", {true, false, false}}}}
  } : legacyBootstrap.contains("profile") ? legacyBootstrap["profile"] : ExportCloudBootstrap("")["profile"];
  profile["expdiff"] = 0; profile["buffs"] = nlohmann::json::array();
  profile["exp_progress_seconds"] = 0; profile["online"] = false;
  profile["buycost"] = 0; profile["revertgain"] = 0; profile["star_available"] = false;
  profile["cloud"] = CloudGame::GetInstance()->Status();
  return profile;
}
float DataManager::CloudTaskProgress() {
  auto state = GetCloudSnapshot();
  if (state.is_null() || state["tasks"]["current"].is_null()) return 0;
  const auto& task = state["tasks"]["current"];
  return task.value("cost", 0.0) > 0 ? static_cast<float>(task.value("elapsed_seconds", 0.0) / task["cost"].get<double>()) : 0;
}
