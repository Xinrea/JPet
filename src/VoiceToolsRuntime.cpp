#include "VoiceTools.hpp"
#include "VoiceToolsNetwork.hpp"
#include "VoicePlatform.hpp"
#include "DataManager.hpp"
#include "CloudGame.hpp"
#include "BuffManager.hpp"
#include "LAppDefine.hpp"
#include "LAppPal.hpp"
#include "Platform.hpp"

namespace Voice {
namespace {
using nlohmann::json;
json Fail(const std::string& error) { return {{"ok", false}, {"error", error}}; }
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

std::pair<std::string, std::string> Credentials() {
  auto* data = DataManager::GetInstance();
  std::string error;
  return {data->GetConfig<std::string>("voice", "workspace_id", ""),
    LoadApiKey(LAppPal::WStringToString(LAppDefine::documentPath), error)};
}
json ReadDesktop(const json& query) {
  const auto [workspace, key] = Credentials();
  if (!ValidWorkspace(workspace) || !ValidApiKey(key)) return Fail("请在语音设置中保存有效的 API Key 和业务空间 ID");
  DesktopImage image;
  std::string error;
  if (!CaptureDesktop(query.at("display").get<int>(), image, error)) return Fail(error);
  return DescribeDesktop(query, image, workspace, key);
}
json ReadWeb(const json& query) {
  const auto [workspace, key] = Credentials();
  return SearchWeb(query, workspace, key);
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
  return {ReadGame, [](const json& command) { return CloudGame::GetInstance()->Command(command); },
    ReadDesktop, ReadWeb, ReadBilibili, ::Platform::OpenWebURL};
}
} // namespace Voice
