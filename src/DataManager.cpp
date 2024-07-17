#include "DataManager.hpp"

#include "LAppDefine.hpp"
#include "LAppPal.hpp"

bool DataManager::init() {
  const std::string configPath = LAppDefine::documentPath + "/config.toml";
  try {
    data = toml::parse_file(configPath);
  } catch (const toml::parse_error& err) {
    LAppPal::PrintLog("Failed to parse config file: %s", err.what());
    return false;
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
  *x = data.at("window").as_table()->at("x").value_or(0);
  *y = data.at("window").as_table()->at("y").value_or(0);
}

void DataManager::UpdateWindowPos(int x, int y) {
  // update x,y in data
  data.insert_or_assign("window", toml::table{{"x", x}, {"y", y}});
}

void DataManager::GetAudio(int* volume, bool* mute) {
  // get volume, mute from data
  *volume = data.at("audio").as_table()->at("volume").value_or(20);
  *mute = data.at("audio").as_table()->at("mute").value_or(false);
}

void DataManager::UpdateAudio(int volume, bool mute) {
  // update volume in data
  data.insert_or_assign("audio",
                        toml::table{{"volume", volume}, {"mute", mute}});
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
  // get scale, green, rateLimit from data
  *scale = data.at("display").as_table()->at("scale").value_or(1);
  *green = data.at("display").as_table()->at("green").value_or(false);
  *rateLimit = data.at("display").as_table()->at("rateLimit").value_or(false);
}

void DataManager::UpdateDisplay(float scale, bool green, bool rateLimit) {
  // update scale, green, rateLimit in data
  data.insert_or_assign("display", toml::table{{"scale", scale},
                                               {"green", green},
                                               {"rateLimit", rateLimit}});
}

void DataManager::GetNotify(std::vector<std::string>* followList, bool* dynamic,
                            bool* live, bool* update) {
  // get followList, dynamic, live, update from data
  if (data.contains("notify")) {
    auto notifyTable = data.at("notify").as_table();
    *dynamic = notifyTable->at("dynamic").value_or(false);
    *live = notifyTable->at("live").value_or(false);
    *update = notifyTable->at("update").value_or(false);
    auto followListArray = notifyTable->at("followList").as_array();
    for (const auto& follow : *followListArray) {
      followList->push_back(follow.as_string()->get());
    }
  }
}

void DataManager::UpdateNotify(const std::vector<std::string>& followList,
                               bool dynamic, bool live, bool update) {
  // update followList, dynamic, live, update in data
  toml::array followListArray;
  for (const auto& follow : followList) {
    followListArray.push_back(follow);
  }
  data.insert_or_assign("notify", toml::table{{"dynamic", dynamic},
                                              {"live", live},
                                              {"update", update},
                                              {"followList", followListArray}});
}

void DataManager::GetModalState(std::map<std::string, float>* modalState) {
  // get modalState from data
  if (data.contains("modalState")) {
    auto modalStateTable = data.at("modalState").as_table();
    for (const auto& [key, value] : *modalStateTable) {
      modalState->emplace(key, value.as_floating_point()->get());
    }
  }
}

void DataManager::UpdateModalState(
    const std::map<std::string, float>& modalState) {
  // update modalState in data
  data.insert_or_assign("modalState", toml::table{});
  auto modalStateTable = data.at("modalState").as_table();
  for (const auto& [key, value] : modalState) {
    modalStateTable->insert_or_assign(key, value);
  }
}

void DataManager::Save() {
  const std::string configPath = LAppDefine::documentPath + "/jpet.toml";
  std::ofstream file(configPath);
  if (!file.is_open()) {
    LAppPal::PrintLog("Failed to open config file for writing");
    return;
  }
  file << data;
  file.close();
  LAppPal::PrintLog("Saved config file: %s", configPath.c_str());
}