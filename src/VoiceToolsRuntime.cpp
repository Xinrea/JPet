#include "VoiceTools.hpp"
#include "VoiceToolsNetwork.hpp"
#include "VoiceDeepSeekWeb.hpp"
#include "VoiceWebFetch.hpp"
#include "VoiceDesktop.hpp"
#include "VoicePlatform.hpp"
#include "DataManager.hpp"
#include "GameManager.hpp"
#include "BuffManager.hpp"
#include "LAppDefine.hpp"
#include "LAppPal.hpp"
#include "Platform.hpp"
#include "LAppDelegate.hpp"
#include "PanelServer.hpp"
#include "VoiceSettingsQueue.hpp"
#include "PartStateManager.h"
#include <ctime>
#include <filesystem>

namespace Voice {
namespace {
using nlohmann::json;
json Fail(const std::string& error) { return {{"ok", false}, {"error", error}}; }
SettingsQueue settingsQueue;
const std::pair<const char*, const char*> appearanceParts[] = {
  {"long_hair", "ParamHair"}, {"left_ear", "ParamLEars"}, {"right_ear", "ParamREars"}, {"hat", "ParamHat"},
  {"glasses", "ParamGlasses"}, {"star_eyes", "ParamEyeStar"}, {"dizzy_eyes", "ParamDizzy"}, {"sweat", "ParamSweat"},
  {"dark_face", "ParamBlackFace"}, {"blush", "ParamRedFace"}, {"leg_accessories", "ParamLegs"},
  {"shoes", "ParamShoes"}, {"tail", "ParamTail"}, {"gun", "ParamGun"}
};

json ReadSettingsValues(const std::string& section) {
  auto* data = DataManager::GetInstance();
  auto* app = LAppDelegate::GetInstance();
  std::lock_guard<std::recursive_mutex> lock(data->GameMutex());
  json result = json::object();
  if (section == "all" || section == "audio") {
    int volume; bool mute, idle, touch;
    data->GetAudio(&volume, &mute, &idle, &touch);
    result["audio"] = {{"volume", volume}, {"mute", mute}, {"idle_audio", idle}, {"touch_audio", touch}};
  }
  if (section == "all" || section == "display")
    result["display"] = {{"scale", app->GetScale()}, {"green", app->Green}, {"limit", app->isLimit}};
  if (section == "all" || section == "interaction")
    result["interaction"] = {{"track", data->IsTracking()}, {"dropfile", data->GetDropFile()}};
  if (section == "all" || section == "notifications") {
    bool dynamic, live, update;
    data->GetNotify(&dynamic, &live, &update);
    result["notifications"] = {{"dynamic", dynamic}, {"live", live}, {"update", update}, {"watch_list", data->GetFollowList()}};
  }
  if (section == "all" || section == "shortcuts") {
    result["shortcuts"] = json::array();
    const char* types[] = {"application", "folder", "website", "settings", "disabled"};
    const char* directions[] = {"up", "right", "down", "left"};
    for (int i = 0; i < 4; ++i) {
      const auto prefix = "shortcut." + std::to_string(i);
      const int type = data->GetWithDefault(prefix + ".type", 3);
      result["shortcuts"].push_back({{"direction", directions[i]}, {"shortcut_type", types[type >= 0 && type <= 4 ? type : 4]},
        {"target", type >= 0 && type <= 2 ? data->GetWithDefault(prefix + ".param", "") : ""}});
    }
  }
  if (section == "all" || section == "clothes") {
    result["clothes"] = {{"names", {{"0", "绿色"}, {"1", "粉色"}, {"2", "冬装"}}},
      {"state", GameManager::GetInstance()->Snapshot()["profile"]["clothes"]}};
  }
  if (section == "all" || section == "appearance") {
    const auto parts = PartStateManager::GetInstance()->GetStatus();
    auto& appearance = result["appearance"] = json::object();
    for (const auto& [key, param] : appearanceParts) appearance[key] = parts.at(param);
    appearance["mouth"] = 0;
    for (int i = 1; i <= 6; ++i) if (parts.at("ParamMouth" + std::to_string(i))) { appearance["mouth"] = i; break; }
  }
  return result;
}

json ReadSettings(const std::string& section) {
  auto result = ReadSettingsValues(section);
  if (result.contains("notifications")) {
    // Snapshot watcher names only after releasing GameMutex: the watcher thread
    // can acquire GameMutex while holding its own mutex.
    std::map<std::string, WatchTarget> targets;
    if (auto* manager = LAppDelegate::GetInstance()->GetUserStateManager())
      manager->GetTargetList(targets);
    auto& list = result["notifications"]["watch_list"];
    list = WatchListView(list.get<std::vector<std::string>>(), targets);
  }
  return result;
}

json ApplySettings(const json& request) {
  auto* data = DataManager::GetInstance();
  auto* app = LAppDelegate::GetInstance();
  const auto action = request.at("action").get<std::string>();
  if (action == "get") return {{"ok", true}, {"settings", ReadSettings(request.at("section"))}};
  std::string section;
  if (action == "update") {
    section = request.at("section");
    // Read and merge under one lock so a partial update keeps other settings.
    std::lock_guard<std::recursive_mutex> lock(data->GameMutex());
    auto values = ReadSettingsValues(section).at(section);
    values.update(request.at("settings"));
    if (section == "audio") data->UpdateAudio(values.at("volume"), values.at("mute"), values.at("idle_audio"), values.at("touch_audio"));
    else if (section == "display") {
      // Both GLFW calls and live flags are applied on the render thread.
      if (request["settings"].contains("green")) app->SetGreen(values.at("green"));
      if (request["settings"].contains("limit")) app->SetLimit(values.at("limit"));
      if (request["settings"].contains("scale")) app->SetScale(values.at("scale"));
      data->UpdateDisplay(app->GetScale(), app->Green, app->isLimit);
    } else if (section == "interaction") {
      data->IsTracking(values.at("track"));
      data->UpdateDropFile(values.at("dropfile"));
    } else if (section == "notifications") {
      data->UpdateNotify(values.at("dynamic"), values.at("live"), values.at("update"));
      app->DynamicNotify = values.at("dynamic");
      app->LiveNotify = values.at("live");
      app->UpdateNotify = values.at("update");
    } else if (section == "appearance") {
      const auto& patch = request.at("settings");
      std::map<std::string, bool> parts;
      for (const auto& [key, param] : appearanceParts) if (patch.contains(key)) parts[param] = patch.at(key);
      if (!PartStateManager::GetInstance()->SetAppearance(parts, patch.value("mouth", 0))) return Fail("角色模型尚未就绪，无法调整装扮");
    }
    data->Save();
  } else if (action == "add_watch" || action == "remove_watch") {
    section = "notifications";
    // The watcher mutex must not be taken while holding GameMutex.
    ReadSettingsValues(section); // Initialize notify defaults before changing its list.
    if (!app->GetUserStateManager()) return Fail("通知服务尚未就绪");
    const auto uid = request.at("uid").get<std::string>();
    if (action == "add_watch") { app->AddWatch(uid); data->AddFollow(uid); }
    else { app->RemoveWatch(uid); data->RemoveFollow(uid); }
    data->Save();
  } else if (action == "set_shortcut") {
    section = "shortcuts";
    const auto type = request.at("shortcut_type").get<std::string>();
    const auto target = request.at("target").get<std::string>();
    if (type == "application" || type == "folder") {
      const auto path = std::filesystem::u8path(target);
      std::error_code error;
      if (!path.is_absolute() || !std::filesystem::exists(path, error) || error) return Fail("轮盘入口的本机路径不存在或无法访问");
      if (type == "folder" && !std::filesystem::is_directory(path, error)) return Fail("文件夹入口必须指向一个目录");
    }
    const auto direction = request.at("direction").get<std::string>();
    const int index = direction == "up" ? 0 : direction == "right" ? 1 : direction == "down" ? 2 : 3;
    const int value = type == "application" ? 0 : type == "folder" ? 1 : type == "website" ? 2 : type == "settings" ? 3 : 4;
    std::lock_guard<std::recursive_mutex> lock(data->GameMutex());
    const auto prefix = "shortcut." + std::to_string(index);
    data->SetRaw(prefix + ".type", value);
    data->SetRaw<std::string>(prefix + ".param", target);
  }
  PanelServer::GetInstance()->Notify("SETTINGS_UPDATE");
  return {{"ok", true}, {"action", action}, {"settings", ReadSettings(section)}};
}

json ReadGame(const json& query) {
  auto game = GameManager::GetInstance()->Snapshot();
  game["profile"]["medal_level"] = BuffManager::GetInstance()->MedalLevel();
  return GameView(game, query.at("section").get<std::string>(), query.value("offset", size_t{0}), query.value("limit", size_t{5}));
}

// The screenshot goes to the chat model as an image (see ToolImageField).
json ReadDesktop(const json& query) {
  DesktopImage image;
  std::string error;
  if (!CaptureDesktop(query.at("display").get<int>(), image, error)) return Fail(error);
  return {{"ok", true}, {"captured_at", static_cast<int64_t>(std::time(nullptr))}, {"width", image.width}, {"height", image.height},
    {"display", query.at("display")}, {"display_count", image.displayCount}, {"external_content", true},
    {ToolImageField, EncodeBase64(image.jpeg)}};
}
json ReadBilibili(const json& query) {
  auto* data = DataManager::GetInstance();
  std::string cookies, uid;
  {
    std::lock_guard<std::recursive_mutex> lock(data->GameMutex());
    cookies = data->GetWithDefault("cookies", std::string{});
    uid = data->GetWithDefault("uid", std::string{});
  }
  return QueryBilibili(query, cookies, uid);
}
// Resolve the selected service at execution time; keys stay in this adapter,
// never in tool arguments, results, or conversation history.
json ReadWebTool(const json& request, const std::function<bool()>& cancelled, bool fetch) {
  if (cancelled && cancelled()) return Fail("网页工具已取消");
  const auto settings = DataManager::GetInstance()->GetVoiceSettings();
  if (settings.at("chat_provider") != "deepseek")
    return fetch ? FetchWebPage(request.at("url").get<std::string>(), cancelled) : QueryWebSearch(request, cancelled);
  std::string error;
  const auto key = LoadChatApiKey(LAppPal::WStringToString(LAppDefine::documentPath), "deepseek", error);
  if (!error.empty() || key.empty()) return Fail("无法读取DeepSeek API Key，请在对话设置中重新保存");
  // Do not silently switch services after a paid API error or send a DeepSeek
  // key to the user's third-party base URL.
  return fetch ? QueryDeepSeekFetch(request, key, cancelled) : QueryDeepSeekSearch(request, key, cancelled);
}
} // namespace

ToolDependencies MakeToolDependencies() {
  settingsQueue.Start();
  return {ReadGame, [](const json& command) { return GameManager::GetInstance()->Command(command); },
    ReadDesktop, ReadBilibili, ::Platform::OpenWebURL,
    [](const json& request, const std::function<bool()>& cancelled) {
      return settingsQueue.Invoke([request] { return ApplySettings(request); }, cancelled);
    }, [](const json& request, const std::function<bool()>& cancelled) { return ReadWebTool(request, cancelled, false); },
    [](const json& request, const std::function<bool()>& cancelled) { return ReadWebTool(request, cancelled, true); }};
}
void DrainSettingsTools() { settingsQueue.Drain(); }
void CancelSettingsTools() { settingsQueue.Cancel(); }
void StopSettingsTools() { settingsQueue.Cancel(true); }
} // namespace Voice
