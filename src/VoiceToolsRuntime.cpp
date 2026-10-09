#include "PowerLiveAccount.hpp"
#include "VoiceTools.hpp"
#include "VoiceToolsNetwork.hpp"
#include "VoicePlatform.hpp"
#include "DataManager.hpp"
#include "CloudGame.hpp"
#include "BuffManager.hpp"
#include "LAppDefine.hpp"
#include "LAppPal.hpp"
#include "Platform.hpp"
#include "LAppDelegate.hpp"
#include "PanelServer.hpp"
#include "VoiceSettingsQueue.hpp"
#include "PartStateManager.h"
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

json ReadSettings(const std::string& section) {
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
    const auto snapshot = data->GetCloudSnapshot();
    const auto profile = data->GetCloudProfile();
    result["clothes"] = {{"names", {{"0", "绿色"}, {"1", "粉色"}, {"2", "冬装"}}},
      {"state", profile.at("clothes")}, {"online", profile.value("online", false)},
      {"confirmed", snapshot.is_object() && snapshot.contains("revision")}};
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
    auto values = ReadSettings(section).at(section);
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
    ReadSettings(section); // Initialize notify defaults before changing its list.
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
  auto* data = DataManager::GetInstance();
  const auto section = query.value("section", std::string("all"));
  if (section == "rank") {
    const auto uid = data->GetWithDefault("uid", std::string{});
    return ReadGameRank(query, CloudGame::ServiceUrl(), uid);
  }
  std::lock_guard<std::recursive_mutex> lock(data->GameMutex());
  auto snapshot = data->GetCloudSnapshot();
  if (!snapshot.is_object()) snapshot = json::object();
  auto connection = CloudGame::GetInstance()->Status();
  // Lease expiry is authoritative even when the transport is still connected.
  connection["online"] = snapshot.value("online", false);
  auto profile = snapshot.contains("profile") ? snapshot["profile"] : data->GetCloudProfile();
  profile["medal_level"] = BuffManager::GetInstance()->MedalLevel();
  return GameView(snapshot, profile,
    snapshot.contains("tasks") ? snapshot["tasks"] : data->GetTaskState(),
    snapshot.contains("achievements") ? snapshot["achievements"] : data->GetAchievementState(), connection,
    {{"uid", data->GetWithDefault("uid", std::string{})}, {"name", data->GetWithDefault("uname", std::string{})}}, section);
}

struct AICredentials { std::string workspace, key, service, error; };
AICredentials Credentials() {
  auto* data = DataManager::GetInstance();
  AICredentials result;
  if (data->GetConfig<std::string>("voice", "provider", "custom") == "jpet") {
    result.service = CloudGame::ServiceUrl(); result.key = PowerLiveAccount::Instance().AccessToken(result.error);
  } else {
    result.workspace = data->GetConfig<std::string>("voice", "workspace_id", "");
    result.key = LoadApiKey(LAppPal::WStringToString(LAppDefine::documentPath), result.error);
    if (result.error.empty() && (!ValidWorkspace(result.workspace) || !ValidApiKey(result.key))) result.error = "请在语音设置中保存有效的 API Key 和业务空间 ID";
  }
  return result;
}
json ReadDesktop(const json& query) {
  const auto credentials = Credentials();
  if (!credentials.error.empty()) return Fail(credentials.error);
  DesktopImage image;
  std::string error;
  if (!CaptureDesktop(query.at("display").get<int>(), image, error)) return Fail(error);
  return DescribeDesktop(query, image, credentials.workspace, credentials.key, credentials.service);
}
json ReadWeb(const json& query) {
  const auto credentials = Credentials();
  if (!credentials.error.empty()) return Fail(credentials.error);
  return SearchWeb(query, credentials.workspace, credentials.key, credentials.service);
}
json ReadBilibili(const json& query) {
  auto* data = DataManager::GetInstance();
  std::string cookies, uid;
  {
    std::lock_guard<std::recursive_mutex> lock(data->GameMutex());
    cookies = data->GetWithDefault("cookies", std::string{});
    uid = data->GetWithDefault("uid", std::string{});
  }
  return SearchBilibili(query, cookies, uid);
}
} // namespace

ToolDependencies MakeToolDependencies() {
  settingsQueue.Start();
  return {ReadGame, [](const json& command) { return CloudGame::GetInstance()->Command(command); },
    ReadDesktop, ReadWeb, ReadBilibili, ::Platform::OpenWebURL,
    [](const json& request, const std::function<bool()>& cancelled) {
      return settingsQueue.Invoke([request] { return ApplySettings(request); }, cancelled);
    }};
}
void DrainSettingsTools() { settingsQueue.Drain(); }
void CancelSettingsTools() { settingsQueue.Cancel(); }
void StopSettingsTools() { settingsQueue.Cancel(true); }
} // namespace Voice
