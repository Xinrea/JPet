#include "DataManager.hpp"

#include "LAppDefine.hpp"
#include "LAppPal.hpp"
#include "LAppLive2DManager.hpp"

#include <filesystem>

bool DataManager::init() {
  const std::wstring configPath = LAppDefine::documentPath + L"/jpet.toml";
  // check file existence
  if (!std::filesystem::exists(std::filesystem::path(configPath))) {
    // create a new config file
    std::ofstream file(configPath);
    if (!file.is_open()) {
      LAppPal::PrintLog("Failed to create config file: %s", configPath.c_str());
      return false;
    }
    file.close();
    // initialize with default values
    LAppPal::PrintLog("Created config file: %s", configPath.c_str());
  } else {
    try {
      data = toml::parse_file(configPath);
    } catch (const toml::parse_error& err) {
      LAppPal::PrintLog("Failed to parse config file: %s", err.what());
      return false;
    }
  }

  // check reset marker
  const std::wstring markerPath = LAppDefine::documentPath + L"/.reset";
  const std::wstring dataPath = LAppDefine::documentPath + L"/jpet.dat";
  if (IsResetMarked()) {
    std::filesystem::remove(std::filesystem::path(markerPath));
    std::filesystem::remove(std::filesystem::path(dataPath));
    LAppPal::PrintLog(LogLevel::Info, "[DataManager]Data reseted due to marker");
  }
  // initialize game data
  bool firstData = !std::filesystem::exists(std::filesystem::path(dataPath));
  gameData = std::make_shared<GameData>(dataPath);
  if (firstData) {
    AddAttribute("speed", 2);
    AddAttribute("strength", 1);
    AddAttribute("endurance", 1);
    AddAttribute("will", 3);
    AddAttribute("intellect", 4);
  }
  return true;
}

DataManager::DataManager() {
  if (!init()) {
    LAppPal::PrintLog("Failed to initialize DataManager");
  }
}

DataManager* DataManager::GetInstance() {
  static DataManager instance;
  return &instance;
}

void DataManager::GetWindowPos(int* x, int* y) {
  // get x,y from data
  if (!data.contains("window")) {
    // update x,y in data
    data.insert_or_assign("window", toml::table{{"x", 0}, {"y", 0}});
    return;
  }
  *x = data.at("window").as_table()->at("x").value_or(0);
  *y = data.at("window").as_table()->at("y").value_or(0);
}

void DataManager::UpdateWindowPos(int x, int y) {
  // update x,y in data
  data.insert_or_assign("window", toml::table{{"x", x}, {"y", y}});
}

void DataManager::GetAudio(int* volume, bool* mute, bool* idle_audio, bool* touch_audio) {
  // if audio doesn't exist, create it
  if (!data.contains("audio")) {
    data.insert("audio", toml::table{{"volume", 20},
                                     {"mute", false},
                                     {"idle_audio", true},
                                     {"touch_audio", true}});
  }
  // get volume, mute from data
  *volume = data.at("audio").as_table()->at("volume").value_or(20);
  *mute = data.at("audio").as_table()->at("mute").value_or(false);
  *idle_audio = data.at("audio").as_table()->at("idle_audio").value_or(true);
  *touch_audio = data.at("audio").as_table()->at("touch_audio").value_or(true);
}

void DataManager::UpdateAudio(int volume, bool mute, bool idle_audio, bool touch_audio) {
  // update volume in data
  data.insert_or_assign("audio", toml::table{{"volume", volume},
                                             {"mute", mute},
                                             {"idle_audio", idle_audio},
                                             {"touch_audio", touch_audio}});
}

void DataManager::GetShortcut(std::map<std::string, std::string>* shortcuts) {
  // get shortcuts from data
  if (data.contains("shortcuts")) {
    auto shortcutsTable = data.at("shortcuts").as_table();
    for (const auto& [key, value] : *shortcutsTable) {
      shortcuts->emplace(key, value.as_string()->get());
    }
  }
}

void DataManager::UpdateShortcut(const std::string& key,
                                 const std::string& value) {
  // if shortcuts doesn't exist, create it
  if (!data.contains("shortcuts")) {
    data.insert("shortcuts", toml::table{});
  }
  auto shortcuts = data.at("shortcuts").as_table();
  shortcuts->insert_or_assign(key, value);
}

void DataManager::GetDisplay(float* scale, bool* green, bool* rateLimit) {
  if (!data.contains("display")) {
    // update scale, green, rateLimit in data
    data.insert_or_assign(
        "display",
        toml::table{{"scale", 1.0f}, {"green", false}, {"rateLimit", false}});
    return;
  }
  // get scale, green, rateLimit from data
  *scale = data.at("display").as_table()->at("scale").value_or(1.0f);
  *green = data.at("display").as_table()->at("green").value_or(false);
  *rateLimit = data.at("display").as_table()->at("rateLimit").value_or(false);
}

void DataManager::UpdateDisplay(float scale, bool green, bool rateLimit) {
  // update scale, green, rateLimit in data
  data.insert_or_assign("display", toml::table{{"scale", scale},
                                               {"green", green},
                                               {"rateLimit", rateLimit}});
}

bool DataManager::IsTracking() {
  if (!data.contains("other")) {
    data.insert_or_assign("other", toml::table{});
    return true;
  }
  auto other = data.at("other").as_table();
  if (!other->contains("track")) {
    other->insert("track", true);
    return true;
  }
  return data.at("other").as_table()->at("track").value_or(true);
}

void DataManager::IsTracking(bool enable) {
  if (!data.contains("other")) {
    data.insert_or_assign("other", toml::table{});
  }
  auto other = data.at("other").as_table();
  other->insert_or_assign("track", enable);
}

void DataManager::initNotifySection() {
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
  initNotifySection();
  // get followList, dynamic, live, update from data
  auto notifyTable = data.at("notify").as_table();
  *dynamic = notifyTable->at("dynamic").value_or(true);
  *live = notifyTable->at("live").value_or(true);
  *update = notifyTable->at("update").value_or(true);
}

void DataManager::UpdateNotify(
                               bool dynamic, bool live, bool update) {
  // update followList, dynamic, live, update in data
  initNotifySection();
  auto notifyTable = data.at("notify").as_table();
  notifyTable->insert_or_assign("dynamic", dynamic);
  notifyTable->insert_or_assign("live", live);
  notifyTable->insert_or_assign("update", update);
}

std::vector<std::string> DataManager::GetFollowList() {
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
  const std::wstring configPath = LAppDefine::documentPath + L"\\jpet.toml";
  std::ofstream file(configPath);
  if (!file.is_open()) {
    LAppPal::PrintLog("Failed to open config file for writing");
    return;
  }
  file << data;
  file.close();
}

void DataManager::AddExp(bool bonus) {
  int currentExp = GetAttribute("exp");
  int intellect = GetAttribute("intellect");
  int medal = GetWithDefault("medal_level", 0);
  int exp = 1 + ceil(99 * LAppPal::EaseInOut(intellect + medal - 4) / 100);
  if (bonus) {
    exp *= 10;
  }
  AddAttribute("exp", exp);
  LAppPal::PrintLog(LogLevel::Debug, "[DataManager]Added %d exp", exp);
}

std::vector<int> DataManager::GetAttributeList() {
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

void DataManager::AddAttribute(const std::string& key, int value) {
  int current = 0;
  gameData->Get("attr." + key, current);
  // WARN not support negative value yet
  gameData->Update("attr." + key, max(current + value, 0));
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

void DataManager::DumpTask(int id, int start_time, int end_time, int success,
                           int status, int cost_snapshot) {
  gameData->Update("task." + std::to_string(id) + ".start_time", start_time);
  gameData->Update("task." + std::to_string(id) + ".end_time", end_time);
  gameData->Update("task." + std::to_string(id) + ".success", success);
  gameData->Update("task." + std::to_string(id) + ".status", status);
  gameData->Update("task." + std::to_string(id) + ".cost_snapshot",
                   cost_snapshot);
}

void DataManager::SetResetMark() {
  const std::wstring markerPath = LAppDefine::documentPath + L"/.reset";
  // check file existence
  if (!std::filesystem::exists(std::filesystem::path(markerPath))) {
    // create a new config file
    std::ofstream file(markerPath);
    if (!file.is_open()) {
      LAppPal::PrintLog(LogLevel::Error, "[DataManager]Failed to create marker file: %s", markerPath.c_str());
      return;
    }
    file.close();
    // initialize with default values
    LAppPal::PrintLog(LogLevel::Debug, "[DataManager]Created marker file: %s", markerPath.c_str());
  }
}

bool DataManager::IsResetMarked() {
  const std::wstring markerPath = LAppDefine::documentPath + L"/.reset";
  // check file existence
  return std::filesystem::exists(std::filesystem::path(markerPath));
}
