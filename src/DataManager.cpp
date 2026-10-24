#include "DataManager.hpp"

#include "LAppDefine.hpp"
#include "LAppPal.hpp"
#include "PanelServer.hpp"
#include "VoicePlatform.hpp"
#include "VoiceCascade.hpp"
#include "VoiceCommon.hpp"

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
  // initialize game data
  gameData = std::make_shared<GameData>(oldDataPath);
  if (!gameData->Initialized()) {
    LAppPal::PrintLog(LogLevel::Error, "[DataManager]Failed to initialize GameData");
    return false;
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
  // Fall back to defaults when a saved value is no longer valid or offered.
  auto chatProvider = GetConfig<std::string>("voice", "chat_provider", "");
  auto chatModel = GetConfig<std::string>("voice", "chat_model", "");
  auto chatUrl = GetConfig<std::string>("voice", "chat_base_url", "");
  if (!chatUrl.empty() && !Voice::ValidChatBaseUrl(chatUrl)) chatUrl.clear();
  auto chatKeys = nlohmann::json::object();
  for (const auto& provider : Voice::ChatProviders())
    chatKeys[provider.id] = GetConfig<bool>("voice", std::string("chat_key_") + provider.id, false);
  if (!VoiceTable().contains("chat_provider")) {
    // Before chat services: a custom endpoint is now a third-party service with
    // the same key; Bailian chat (no endpoint) has no counterpart.
    chatProvider = chatUrl.empty() ? "" : "thirdparty";
    if (chatUrl.empty()) chatModel.clear();
    chatKeys["thirdparty"] = !chatUrl.empty() && GetConfig<bool>("voice", "has_chat_api_key", false);
  }
  if (!Voice::FindChatProvider(chatProvider) || (chatProvider == "thirdparty" && chatUrl.empty())) chatProvider.clear();
  if (!Voice::ValidChatModel(chatModel)) chatModel.clear();
  const auto contextDefault = Voice::kDefaultChatContextWindow;
  const auto* savedContextWindow = VoiceTable()["chat_context_window"].as_integer();
  const auto chatContextWindow = savedContextWindow ?
      Voice::ParseChatContextWindow(savedContextWindow->get()).value_or(contextDefault) : contextDefault;
  auto providers = nlohmann::json::array();
  for (const auto& provider : Voice::ChatProviders())
    providers.push_back({{"id", provider.id}, {"name", provider.name}, {"base_url", provider.baseUrl}});
  auto inputMode = GetConfig<std::string>("voice", "input_mode", "toggle");
  if (inputMode != "hold") inputMode = "toggle";
  auto inputDevice = GetConfig<std::string>("voice", "input_device", "");
  if (!Voice::ValidAudioDeviceId(inputDevice)) inputDevice.clear();
  auto outputDevice = GetConfig<std::string>("voice", "output_device", "");
  if (!Voice::ValidAudioDeviceId(outputDevice)) outputDevice.clear();
  return {{"input_mode", inputMode},
          {"input_device", inputDevice}, {"output_device", outputDevice},
          {"chat_provider", chatProvider}, {"chat_model", chatModel}, {"chat_base_url", chatUrl},
          // Whether the model reads images, i.e. may be offered view_desktop.
          {"chat_vision", !chatModel.empty() && GetConfig<bool>("voice", "chat_vision", false)},
          {"chat_context_window", chatContextWindow},
          {"chat_keys", chatKeys}, {"chat_providers", providers}};
}

nlohmann::json DataManager::LoadVoiceHistory() {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  return nlohmann::json::parse(GetWithDefault("voice.history", std::string{}), nullptr, false);
}

void DataManager::SaveVoiceHistory(const nlohmann::json& history) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  gameData->UpdateBatch({}, {{"voice.history", history.dump()}});
}

bool DataManager::UpdateVoiceSettings(const nlohmann::json& payload, std::string& error) {
  if (!payload.is_object()) { error = "语音设置格式不正确"; return false; }
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  const auto current = GetVoiceSettings();
  // Every field is optional; a missing field keeps the saved value.
  const auto text = [&](const char* key, std::string& value) {
    if (!payload.contains(key)) { value = current.value(key, std::string{}); return true; }
    if (!payload[key].is_string()) return false;
    value = Voice::Trim(payload[key].get<std::string>());
    return true;
  };
  std::string inputMode, inputDevice, outputDevice, chatProvider, chatModel, chatUrl;
  if (!text("input_mode", inputMode) || (inputMode != "toggle" && inputMode != "hold")) {
    error = "请选择开关或按住的快捷键方式";
    return false;
  }
  // Not checked against connected devices: an unplugged device stays selected
  // and the default is used until it returns.
  if (!text("input_device", inputDevice) || !Voice::ValidAudioDeviceId(inputDevice) ||
      !text("output_device", outputDevice) || !Voice::ValidAudioDeviceId(outputDevice)) {
    error = "请重新选择麦克风和扬声器";
    return false;
  }
  // The model and endpoint belong to the chat service; choosing another
  // service without them clears the saved ones.
  if (!text("chat_provider", chatProvider) || (!chatProvider.empty() && !Voice::FindChatProvider(chatProvider))) {
    error = "请选择大模型服务";
    return false;
  }
  const bool providerChanged = chatProvider != current.value("chat_provider", std::string{});
  if (providerChanged && !payload.contains("chat_model")) chatModel.clear();
  else if (!text("chat_model", chatModel) || (!chatModel.empty() && !Voice::ValidChatModel(chatModel))) {
    error = "请选择有效的模型";
    return false;
  }
  if (!text("chat_base_url", chatUrl) || (!chatUrl.empty() && !Voice::ValidChatBaseUrl(chatUrl))) {
    error = "接口地址需以 https:// 开头（本机地址可用 http://），且不能包含账号信息";
    return false;
  }
  if (chatProvider != "thirdparty") chatUrl.clear();
  else if (chatUrl.empty()) { error = "请填写第三方服务的 OpenAI 兼容接口地址"; return false; }
  // Image input is a property of the model: a new model without it is text-only.
  bool chatVision = current.at("chat_vision");
  if (payload.contains("chat_vision")) {
    if (!payload["chat_vision"].is_boolean()) { error = "请选择模型是否支持图片输入"; return false; }
    chatVision = payload["chat_vision"];
  } else if (chatModel != current.value("chat_model", std::string{})) chatVision = false;
  // Capacity belongs to the model AND endpoint, even when both services use the same model id.
  size_t chatContextWindow = current.at("chat_context_window").get<size_t>();
  if (payload.contains("chat_context_window")) {
    const auto capacity = Voice::ParseChatContextWindow(payload["chat_context_window"]);
    if (!capacity) { error = "上下文窗口需为 4096 至 10000000 之间的整数（token）"; return false; }
    chatContextWindow = *capacity;
  } else if (providerChanged || chatModel != current.value("chat_model", std::string{}) ||
             chatUrl != current.value("chat_base_url", std::string{})) {
    chatContextWindow = Voice::kDefaultChatContextWindow;
  }
  // Keys are write-only: a new value replaces the saved one, "clear_*" removes it.
  const auto secret = [&](const char* key, const char* clear, std::string& value, bool& update) {
    update = payload.value(clear, false) || payload.contains(key);
    if (!update || payload.value(clear, false)) return true;
    if (!payload[key].is_string()) return false;
    value = Voice::Trim(payload[key].get<std::string>());
    return value.empty() || Voice::ValidApiKey(value);
  };
  std::string chatKey;
  bool updateChatKey = false;
  if (!secret("chat_api_key", "clear_chat_api_key", chatKey, updateChatKey)) {
    error = "大模型 API Key 格式不正确，请检查是否包含空格";
    return false;
  }
  // A chat key belongs to the selected service.
  if (updateChatKey && chatProvider.empty()) { error = "请先选择大模型服务"; return false; }
  const auto profile = LAppPal::WStringToString(LAppDefine::documentPath);
  if (updateChatKey && !Voice::SaveChatApiKey(profile, chatProvider, chatKey, error)) return false;
  auto& table = VoiceTable();
  // Settings of the removed Omni engine, JPet Server provider, CosyVoice
  // synthesis, single custom chat key and Bailian search/vision key.
  if (table["has_api_key"].value_or(false)) Voice::RemoveLegacyApiKey(profile);
  for (const auto* legacy : {"provider", "model", "voice", "engine", "stt", "tts_model", "tts_voice", "has_chat_api_key",
                             "workspace_id", "has_api_key"})
    table.erase(legacy);
  table.insert_or_assign("input_mode", inputMode);
  table.insert_or_assign("input_device", inputDevice);
  table.insert_or_assign("output_device", outputDevice);
  table.insert_or_assign("chat_provider", chatProvider);
  table.insert_or_assign("chat_model", chatModel);
  table.insert_or_assign("chat_base_url", chatUrl);
  table.insert_or_assign("chat_vision", chatVision);
  table.insert_or_assign("chat_context_window", static_cast<int64_t>(chatContextWindow));
  for (const auto& provider : Voice::ChatProviders())
    table.insert_or_assign(std::string("chat_key_") + provider.id, current["chat_keys"].value(provider.id, false));
  if (updateChatKey) table.insert_or_assign("chat_key_" + chatProvider, !chatKey.empty());
  Save();
  return true;
}

toml::table& DataManager::VoiceTable() {
  if (!data["voice"].is_table()) data.insert_or_assign("voice", toml::table{});
  return *data["voice"].as_table();
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

std::string DataManager::GetWithDefault(const std::string& key, const std::string& default_value) {
  std::string value = default_value;
  if (gameData->Get(key, value)) {
    return value;
  }
  return default_value;
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
