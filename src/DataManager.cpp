#include "DataManager.hpp"

#include "LAppDefine.hpp"
#include "LAppPal.hpp"
#include "LAppLive2DManager.hpp"
#include "PanelServer.hpp"
#include "VoicePlatform.hpp"
#include "VoiceSession.hpp"

#include <filesystem>

bool DataManager::init() {
  const std::wstring configPath = LAppDefine::documentPath + L"/jpet.toml";
  // check file existence
  if (!std::filesystem::exists(std::filesystem::path(configPath))) {
    // create a new config file
    std::ofstream file{std::filesystem::path(configPath)};
    if (!file.is_open()) {
      LAppPal::PrintLog(LogLevel::Error, L"Failed to create config file: %ls", configPath.c_str());
      return false;
    }
    file.close();
    // initialize with default values
    LAppPal::PrintLog(LogLevel::Info, L"Created config file: %ls",
                      configPath.c_str());
  } else {
    try {
      data = toml::parse_file(LAppPal::WStringToString(configPath));
    } catch (const toml::parse_error& err) {
      LAppPal::PrintLog("Failed to parse config file: %s", err.what());
      return false;
    }
  }

  const std::wstring oldDataPath = LAppDefine::documentPath + L"/jpet.dat";
  const std::wstring dataPath = LAppDefine::documentPath + L"/GameData";
  // initialize game data
  bool firstData = !std::filesystem::exists(std::filesystem::path(dataPath)) && !std::filesystem::exists(std::filesystem::path(oldDataPath));
  gameData = std::make_shared<GameData>(oldDataPath);
  if (!gameData->Initialized()) {
    LAppPal::PrintLog(LogLevel::Error, "[DataManager]Failed to initialize GameData");
    return false;
  }
  if (firstData) {
    gameData->UpdateBatch({{"attr.speed", 2}, {"attr.strength", 1},
        {"attr.endurance", 1}, {"attr.will", 3}, {"attr.intellect", 4}});
  }
  legacyBootstrap = nlohmann::json::parse(GetWithDefault("cloud.legacy_bootstrap", std::string{}), nullptr, false);
  if (!legacyBootstrap.is_object()) {
    legacyBootstrap = ExportCloudBootstrap("");
    gameData->UpdateBatch({}, {{"cloud.legacy_bootstrap", legacyBootstrap.dump()}});
  }
  LoadCloudCache(GetWithDefault("uid", std::string{}));
  return true;
}

DataManager::DataManager() {
  if (!init()) {
    LAppPal::PrintLog(LogLevel::Error, "Failed to initialize DataManager");
    throw std::runtime_error("Failed to initialize DataManager");
  }
}

DataManager* DataManager::GetInstance() {
  static DataManager instance;
  return &instance;
}

void DataManager::GetWindowPos(int* x, int* y) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  // get x,y from data
  if (!data.contains("window")) {
    // update x,y in data
    data.insert_or_assign("window", toml::table{{"x", 0}, {"y", 0}});
    return;
  }
  *x = GetConfig("window", "x", 0);
  *y = GetConfig("window", "y", 0);
}

void DataManager::UpdateWindowPos(int x, int y) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  // update x,y in data
  data.insert_or_assign("window", toml::table{{"x", x}, {"y", y}});
}

void DataManager::GetAudio(int* volume, bool* mute, bool* idle_audio, bool* touch_audio) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  // if audio doesn't exist, create it
  if (!data.contains("audio")) {
    data.insert("audio", toml::table{{"volume", 20},
                                     {"mute", false},
                                     {"idle_audio", true},
                                     {"touch_audio", true}});
  }
  // get volume, mute from data
  *volume = GetConfig("audio", "volume", 20);
  *mute = GetConfig("audio", "mute", false);
  *idle_audio = GetConfig("audio", "idle_audio", true);
  *touch_audio = GetConfig("audio", "touch_audio", true);
}

void DataManager::UpdateAudio(int volume, bool mute, bool idle_audio, bool touch_audio) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  // update volume in data
  data.insert_or_assign("audio", toml::table{{"volume", volume},
                                             {"mute", mute},
                                             {"idle_audio", idle_audio},
                                             {"touch_audio", touch_audio}});
}

void DataManager::GetDisplay(float* scale, bool* green, bool* rateLimit) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  if (!data.contains("display")) {
    // update scale, green, rateLimit in data
    data.insert_or_assign(
        "display",
        toml::table{{"scale", 1.0f}, {"green", false}, {"rateLimit", false}});
    return;
  }
  // get scale, green, rateLimit from data
  *scale = GetConfig("display", "scale", 1.0f);
  *green = GetConfig("display", "green", false);
  *rateLimit = GetConfig("display", "rateLimit", false);
}

nlohmann::json DataManager::GetVoiceSettings() {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  return {{"workspace_id", GetConfig<std::string>("voice", "workspace_id", "")},
          {"has_api_key", GetConfig<bool>("voice", "has_api_key", false)},
          {"model", Voice::Model}};
}

nlohmann::json DataManager::LoadVoiceHistory() {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  return nlohmann::json::parse(GetWithDefault("voice.history", std::string{}), nullptr, false);
}

void DataManager::SaveVoiceHistory(const nlohmann::json& history) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  gameData->UpdateBatch({}, {{"voice.history", history.dump()}});
}

bool DataManager::UpdateVoiceSettings(const std::string& workspace,
                                      const std::string* apiKey, std::string& error) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  if (!workspace.empty() && !Voice::ValidWorkspace(workspace)) {
    error = "业务空间 ID 应为 1～63 位字母、数字或连字符，且不能以连字符开头或结尾";
    return false;
  }
  if (apiKey && !apiKey->empty() && !Voice::ValidApiKey(*apiKey)) {
    error = "API Key 格式不正确，请检查是否包含空格";
    return false;
  }
  if (apiKey && !Voice::SaveApiKey(LAppPal::WStringToString(LAppDefine::documentPath), *apiKey, error)) return false;
  const bool hasKey = apiKey ? !apiKey->empty() : GetConfig<bool>("voice", "has_api_key", false);
  data.insert_or_assign("voice", toml::table{{"workspace_id", workspace}, {"has_api_key", hasKey}});
  Save();
  return true;
}

void DataManager::UpdateDisplay(float scale, bool green, bool rateLimit) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  // update scale, green, rateLimit in data
  data.insert_or_assign("display", toml::table{{"scale", scale},
                                               {"green", green},
                                               {"rateLimit", rateLimit}});
}

bool DataManager::GetDropFile() {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  if (!data.contains("other")) {
    data.insert_or_assign("other", toml::table{});
    return true;
  }
  auto other = data.at("other").as_table();
  if (!other->contains("dropfile")) {
    other->insert("dropfile", true);
    return true;
  }
  return GetConfig("other", "dropfile", true);
}

void DataManager::UpdateDropFile(bool enable) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  if (!data.contains("other")) {
    data.insert_or_assign("other", toml::table{});
  }
  auto other = data.at("other").as_table();
  other->insert_or_assign("dropfile", enable);
}

bool DataManager::IsTracking() {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  if (!data.contains("other")) {
    data.insert_or_assign("other", toml::table{});
    return true;
  }
  auto other = data.at("other").as_table();
  if (!other->contains("track")) {
    other->insert("track", true);
    return true;
  }
  return GetConfig("other", "track", true);
}

void DataManager::IsTracking(bool enable) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  if (!data.contains("other")) {
    data.insert_or_assign("other", toml::table{});
  }
  auto other = data.at("other").as_table();
  other->insert_or_assign("track", enable);
}

void DataManager::initNotifySection() {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  if (!data.contains("notify")) {
    data.insert("notify", toml::table{});
    auto notifyTable = data.at("notify").as_table();
    notifyTable->insert("followList", toml::array{});
    notifyTable->insert("dynamic", true);
    notifyTable->insert("live", true);
    notifyTable->insert("update", true);
  }
}

void DataManager::GetNotify(bool *dynamic, bool *live, bool *update) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  initNotifySection();
  // get followList, dynamic, live, update from data
  *dynamic = GetConfig("notify", "dynamic", true);
  *live = GetConfig("notify", "live", true);
  *update = GetConfig("notify", "update", true);
}

void DataManager::UpdateNotify(bool dynamic, bool live, bool update) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  // update followList, dynamic, live, update in data
  initNotifySection();
  auto notifyTable = data.at("notify").as_table();
  notifyTable->insert_or_assign("dynamic", dynamic);
  notifyTable->insert_or_assign("live", live);
  notifyTable->insert_or_assign("update", update);
}

std::vector<std::string> DataManager::GetFollowList() {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  std::vector<std::string> ret;
  initNotifySection();
  auto notifyTable = data.at("notify").as_table();
  auto& followListArray = *notifyTable->get_as<toml::array>("followList");
  for (const auto& follow : followListArray) {
    ret.push_back(follow.as_string()->get());
  }
  return ret;
}

void DataManager::AddFollow(const std::string& uid) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  auto notifyTable = data.at("notify").as_table();
  auto& followListArray = *notifyTable->get_as<toml::array>("followList");
  // check exist
  for (const auto& follow : followListArray) {
    if (follow.as_string()->get() == uid) {
      return;
    }
  }
  followListArray.push_back(uid);
}

void DataManager::RemoveFollow(const std::string& uid) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  auto notifyTable = data.at("notify").as_table();
  auto& followListArray = *notifyTable->get_as<toml::array>("followList");
  auto iter = followListArray.cbegin();
  for (;iter != followListArray.cend();) {
    if (iter->as_string()->get() == uid) {
      iter = followListArray.erase(iter);
    } else {
      iter++;
    }
  }
}

void DataManager::Save() {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  const std::filesystem::path configPath = std::filesystem::path(LAppDefine::documentPath) / "jpet.toml";
  std::ofstream file(configPath);
  if (!file.is_open()) {
    LAppPal::PrintLog("Failed to open config file for writing");
    return;
  }
  file << data;
  file.close();
}

int DataManager::CurrentExpDiff() {
  return GetCloudProfile().value("expdiff", 0);
}

std::vector<int> DataManager::GetAttributeList() {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  std::vector<int> attributes;
  for (const auto& attr : {"speed", "endurance", "strength", "will",
                           "intellect", "exp", "buycnt"}) {
    int value = GetAttribute(attr);
    attributes.push_back(value);
  }
  return attributes;
}

int DataManager::GetWithDefault(const std::string& key, int default_value) {
  int value = 0;
  if (gameData->Get(key, value)) {
    return value;
  }
  return default_value;
}

float DataManager::GetWithDefault(const std::string& key, float default_value) {
  float value = 0;
  if (gameData->Get(key, value)) {
    return value;
  }
  return default_value;
}

string DataManager::GetWithDefault(const std::string& key, const string& default_value) {
  string value = default_value;
  if (gameData->Get(key, value)) {
    return value;
  }
  return default_value;
}

void DataManager::PostProcess(const std::string& key, int value) {
  // change clothes
  if (key == "clothes.current") {
    LAppLive2DManager::GetInstance()->SwitchClothes(value);
  }
}

int DataManager::GetAttribute(const std::string& key) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  int value = 0;
  try {
    gameData->Get("attr." + key, value);
  } catch (const std::exception& e) {
    LAppPal::PrintLog(LogLevel::Error,
                      "[DataManager]Get Attribute failed %s: %s reset to 0",
                      key.c_str(), e.what());
    gameData->Update("attr." + key, 0);
  }
  return value;
}

std::vector<int> DataManager::TaskStatus(int id) {
  std::vector<int> status_vec;
  int start_time = 0;
  int end_time = 0;
  int success = 0;
  int status = 0;
  int cost_snapshot = 0;
  gameData->Get("task." + std::to_string(id) + ".start_time", start_time);
  gameData->Get("task." + std::to_string(id) + ".end_time", end_time);
  gameData->Get("task." + std::to_string(id) + ".success", success);
  gameData->Get("task." + std::to_string(id) + ".status", status);
  gameData->Get("task." + std::to_string(id) + ".cost_snapshot", cost_snapshot);
  status_vec.push_back(start_time);
  status_vec.push_back(end_time);
  status_vec.push_back(success);
  status_vec.push_back(status);
  status_vec.push_back(cost_snapshot);
  return status_vec;
}
