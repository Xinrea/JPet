#include "DataManager.hpp"

#include "BuffManager.hpp"
#include "LAppDefine.hpp"
#include "LAppPal.hpp"
#include "LAppLive2DManager.hpp"
#include "PanelServer.hpp"

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
    LAppPal::PrintLog(LogLevel::Info, L"Created config file: %ls",
                      configPath.c_str());
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
  const std::wstring oldDataPath = LAppDefine::documentPath + L"/jpet.dat";
  const std::wstring dataPath = LAppDefine::documentPath + L"/GameData";
  if (IsResetMarked()) {
    std::filesystem::remove(std::filesystem::path(markerPath));
    std::filesystem::remove(std::filesystem::path(oldDataPath));
    std::filesystem::remove_all(std::filesystem::path(dataPath));
    LAppPal::PrintLog(LogLevel::Info, "[DataManager]Data reseted due to marker");
  }
  // initialize game data
  bool firstData = !std::filesystem::exists(std::filesystem::path(dataPath)) && !std::filesystem::exists(std::filesystem::path(oldDataPath));
  gameData = std::make_shared<GameData>(oldDataPath);
  if (!gameData->Initialized()) {
    LAppPal::PrintLog(LogLevel::Error, "[DataManager]Failed to initialize GameData");
    return false;
  }
  if (firstData) {
    SetRaw("data.version", 1);
    AddAttribute("speed", 2);
    AddAttribute("strength", 1);
    AddAttribute("endurance", 1);
    AddAttribute("will", 3);
    AddAttribute("intellect", 4);
  }
  
  // check attributes that more than limit by adding 0
  // details are in AddAttribute()
  for (const auto &attr :
       {"speed", "endurance", "strength", "will", "intellect"}) {
    AddAttribute(attr, 0);
  }
  
  // if legacy player
  if (GetWithDefault("data.version", 0) == 0) {
    // prepare some extra benefit
    int extra = 1;
    if (GetWithDefault("clothes.1.active", 0) > 0) {
      extra = 2;
    }
    if (GetWithDefault("clothes.2.active", 0) > 0) {
      extra = 3;
    }
    // save login cookies
    auto cookies = GetWithDefault("cookies", "");
    auto user_agent = GetWithDefault("user-agent", "");
    // drop old data
    gameData->Drop();
    // apply initial
    AddAttribute("speed", 2);
    AddAttribute("strength", 1);
    AddAttribute("endurance", 1);
    AddAttribute("will", 3);
    AddAttribute("intellect", 4);
    // apply benefit
    for (const auto &attr :
         {"speed", "endurance", "strength", "will", "intellect"}) {
      AddAttribute(attr, extra);
    }
    SetRaw("legacy", 1);
    // apply login cookies
    SetRaw("cookies", cookies);
    SetRaw("user-agent", user_agent);
    // set data version
    SetRaw("data.version", 1);
  }
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
  *volume = GetConfig("audio", "volume", 20);
  *mute = GetConfig("audio", "mute", false);
  *idle_audio = GetConfig("audio", "idle_audio", true);
  *touch_audio = GetConfig("audio", "touch_audio", true);
}

void DataManager::UpdateAudio(int volume, bool mute, bool idle_audio, bool touch_audio) {
  // update volume in data
  data.insert_or_assign("audio", toml::table{{"volume", volume},
                                             {"mute", mute},
                                             {"idle_audio", idle_audio},
                                             {"touch_audio", touch_audio}});
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
  *scale = GetConfig("display", "scale", 1.0f);
  *green = GetConfig("display", "green", false);
  *rateLimit = GetConfig("display", "rateLimit", false);
}

void DataManager::UpdateDisplay(float scale, bool green, bool rateLimit) {
  // update scale, green, rateLimit in data
  data.insert_or_assign("display", toml::table{{"scale", scale},
                                               {"green", green},
                                               {"rateLimit", rateLimit}});
}

bool DataManager::GetDropFile() {
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
  if (!data.contains("other")) {
    data.insert_or_assign("other", toml::table{});
  }
  auto other = data.at("other").as_table();
  other->insert_or_assign("dropfile", enable);
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
  return GetConfig("other", "track", true);
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
  *dynamic = GetConfig("notify", "dynamic", true);
  *live = GetConfig("notify", "live", true);
  *update = GetConfig("notify", "update", true);
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

int DataManager::CurrentExpDiff() {
  int currentExp = GetAttribute("exp");
  int intellect = GetAttribute("intellect");
  int starcnt = GetWithDefault("starcnt", 0);
  int medal = BuffManager::GetInstance()->MedalLevel();
  int exp = 1 + ceil(499 * LAppPal::EaseOut(intellect + medal / 3 - 4) / 100);
  BuffManager* bf = BuffManager::GetInstance();
  if (bf->IsDynamic()) {
    exp *= 2;
  }
  if (bf->IsLive()) {
    exp *= 3;
  }
  if (bf->IsGuard()) {
    exp *= 1.5f;
  }
  if (bf->IsFail()) {
    exp *= 1.5f;
  }
  if (bf->IsBirthday()) {
    exp *= 6;
  }
  if (bf->IsLegacy()) {
    exp *= 1.2f;
  }
  exp *= (1 + 0.2 * starcnt);
  return std::min(exp, 99999999);
}

void DataManager::AddExp() {
  int diff = CurrentExpDiff();
  AddAttribute("exp", diff);
  LAppPal::PrintLog(LogLevel::Debug, "[DataManager]Added %d exp", diff);
  PanelServer::GetInstance()->Notify("UPDATE");
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

void DataManager::FetchStar() {
  for (const auto &attr :
       {"speed", "endurance", "strength", "will", "intellect"}) {
    int value = GetAttribute(attr);
    if (value < 53) {
      return;
    }
  }

  for (const auto &attr :
       {"speed", "endurance", "strength", "will", "intellect"}) {
    AddAttribute(attr, -53);
  }
  int current = 0;
  gameData->Get("starcnt", current);
  gameData->Update("starcnt", current + 1);
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

int DataManager::GetAttrLimit() {
  int starcnt = GetWithDefault("starcnt", 0);
  return 100 + starcnt * 10;
}

void DataManager::AddAttribute(const std::string& key, int value) {
  int current = 0;
  gameData->Get("attr." + key, current);
  // WARN not support negative value yet
  int new_value = std::min(std::max(current + value, 0), 99999999);
  // normal attributes are limited
  if (key != "exp" && key != "buycnt") {
    // extra is returned as exp
    int limit = GetAttrLimit();
    if (new_value > limit) {
      AddAttribute("exp", (new_value - limit) * 53000 / 2);
      new_value = limit;
    }
  }
  gameData->Update("attr." + key, new_value);
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
      LAppPal::PrintLog(LogLevel::Error, L"[DataManager]Failed to create marker file: %ls", markerPath.c_str());
      return;
    }
    file.close();
    // initialize with default values
    LAppPal::PrintLog(LogLevel::Debug, L"[DataManager]Created marker file: %ls", markerPath.c_str());
  }
}

bool DataManager::IsResetMarked() {
  const std::wstring markerPath = LAppDefine::documentPath + L"/.reset";
  // check file existence
  return std::filesystem::exists(std::filesystem::path(markerPath));
}
