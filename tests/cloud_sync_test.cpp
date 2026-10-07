// Exercise the native WebSocket transport and RocksDB snapshot cache without GUI.
#include "DataManager.hpp"
#include "CloudGame.hpp"
#include "BuffManager.hpp"
#include "PanelServer.hpp"
#include "Platform.hpp"
#include <cassert>
#include <codecvt>
#include <cstdlib>
#include <iostream>
#include <locale>
#include <thread>

namespace LAppDefine { std::wstring documentPath; const Csm::csmBool DebugLogEnable = false; }
void LAppPal::PrintLog(LogLevel, const char*, ...) {}
void LAppPal::PrintLog(const char*, ...) {}
std::wstring LAppPal::StringToWString(const std::string& text) { return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.from_bytes(text); }
std::string LAppPal::WStringToString(const std::wstring& text) { return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.to_bytes(text); }
void PanelServer::Notify(const std::string&) {}
void PanelServer::Stop() {}
void Platform::Notify(const std::wstring&, const std::wstring&, const std::string&) {}
void BuffManager::thread() {}
std::vector<std::string> BuffManager::GetBuffList() { return {"live"}; }
DataManager::DataManager() {
  data.insert("cloud", toml::table{{"url", "https://ignored.invalid/from-toml"}});
  std::filesystem::create_directories(std::filesystem::path(LAppDefine::documentPath));
  gameData = std::make_shared<GameData>(LAppDefine::documentPath + L"/unused.dat");
  assert(gameData->Initialized());
}
DataManager* DataManager::GetInstance() { static DataManager instance; return &instance; }
void DataManager::Save() {}
void DataManager::PostProcess(const std::string&, int) {}
int DataManager::GetWithDefault(const std::string& key, int fallback) { int value = fallback; gameData->Get(key, value); return value; }
std::string DataManager::GetWithDefault(const std::string& key, const std::string& fallback) { auto value = fallback; gameData->Get(key, value); return value; }
int DataManager::GetAttribute(const std::string& key) { return GetWithDefault("attr." + key, 0); }
std::vector<int> DataManager::TaskStatus(int id) {
  std::vector<int> result;
  for (auto key : {"start_time", "end_time", "success", "status", "cost_snapshot"}) result.push_back(GetWithDefault("task." + std::to_string(id) + "." + key, 0));
  return result;
}

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs = 5000) {
  for (int i = 0; i < timeoutMs / 25; ++i) { if (predicate()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(25)); }
  return false;
}
int main(int argc, char** argv) {
  assert(argc == 2);
  LAppDefine::documentPath = LAppPal::StringToWString(argv[1]);
  auto dm = DataManager::GetInstance();
  dm->SetRaw("uid", std::string{"123"}); dm->SetRaw("uname", std::string{"轴伊"}); dm->SetRaw("cookies", std::string{"local-test-cookie"});
  using Json = nlohmann::json;
  const auto url = CloudGame::ServiceUrl();
  const std::string testOrigin = "http://127.0.0.1:";
  assert(url.rfind(testOrigin, 0) == 0);
  const int port = std::stoi(url.substr(testOrigin.size()));
  httplib::Client fixture("127.0.0.1", port);
  setenv("JPET_CLOUD_URL", "https://ignored.invalid/from-environment", 1);
  assert(dm->GetConfig("cloud", "url", std::string{}) == "https://ignored.invalid/from-toml");
  auto client = CloudGame::GetInstance();
  assert(client->Status()["configured"] == true);
  assert(client->Status()["transport"] == "websocket");
  client->Start();
  assert(waitUntil([&] { return client->Online(); }));
  assert(dm->GetCloudProfile()["attributes"]["exp"] == 100);
  // An idle connection must survive until the scheduled 15-second heartbeat.
  assert(waitUntil([&] { return Json::parse(fixture.Get("/stats")->body)["heartbeats"].get<int>() >= 1; }, 20000));
  assert(Json::parse(fixture.Get("/stats")->body)["connections"] == 1);
  auto error = client->Command({{"type", "attr.buy"}, {"attr", "speed"}});
  assert(!error.empty());
  assert(waitUntil([&] { return client->Online() && dm->GetWithDefault("cloud.pending", std::string{}).empty(); }));
  assert(dm->GetCloudProfile()["attributes"]["speed"] == 3);
  assert(dm->GetCloudProfile()["attributes"]["exp"] == 90);
  auto stats = Json::parse(fixture.Get("/stats")->body);
  assert(stats["commands"] == 1 && stats["attempts"].get<int>() >= 2 && stats["connections"] == 2);
  const int previousHeartbeats = stats["heartbeats"].get<int>();
  client->Wake();
  assert(waitUntil([&] { return Json::parse(fixture.Get("/stats")->body)["heartbeats"].get<int>() > previousHeartbeats; }));
  assert(Json::parse(fixture.Get("/stats")->body)["connections"] == 2);
  // Business failures are final; transient server failures keep the same ID pending.
  assert(client->Command({{"type", "task.start"}, {"id", -1}}) == "任务不存在");
  assert(dm->GetWithDefault("cloud.pending", std::string{}).empty() && client->Online());
  assert(!client->Command({{"type", "share"}, {"enabled", true}}).empty());
  assert(waitUntil([&] { return client->Online() && dm->GetWithDefault("cloud.pending", std::string{}).empty() && dm->GetCloudSnapshot().value("share", false); }));
  stats = Json::parse(fixture.Get("/stats")->body);
  assert(stats["connections"] == 2 && stats["commands"] == 2);
  // Receive a server-initiated update without another request or heartbeat.
  assert(fixture.Get("/push"));
  assert(waitUntil([&] { return dm->GetAttribute("exp") == 95; }));
  auto state = dm->GetCloudSnapshot();
  auto old = state; old["revision"] = 0; old["profile"]["attributes"]["exp"] = 999;
  dm->ApplyCloudSnapshot(old);
  assert(dm->GetAttribute("exp") == 95);
  old = state; old["server_time"] = state["server_time"].get<int64_t>() - 1;
  old["profile"]["attributes"]["exp"] = 999; dm->ApplyCloudSnapshot(old);
  assert(dm->GetAttribute("exp") == 95);
  old["uid"] = "456"; old["revision"] = 100000; dm->ApplyCloudSnapshot(old);
  assert(dm->GetAttribute("exp") == 95);
  dm->SetRaw("uid", std::string{"456"});
  assert(dm->GetCloudSnapshot().is_null());
  assert(dm->GetCloudProfile()["attributes"]["exp"] == 0);
  dm->SetRaw("uid", std::string{"123"});
  auto leased = state; leased["lease_remaining_ms"] = 20;
  leased["revision"] = state["revision"].get<int64_t>() + 1;
  dm->ApplyCloudSnapshot(leased);
  std::this_thread::sleep_for(std::chrono::milliseconds(40));
  const auto expired = dm->GetCloudProfile();
  assert(expired["online"] == false && expired["attributes"]["exp"] == 95);
  assert(expired["exp_progress_seconds"].get<double>() >= 10.02);
  setenv("JPET_CLOUD_URL", "https://other.example.com", 1);
  assert(CloudGame::ServiceUrl() == url);
  assert(dm->GetCloudProfile()["attributes"]["exp"] == 95);
  dm->LoadCloudCache("123", "https://other.example.com");
  auto otherService = state; otherService["revision"] = 1; otherService["profile"]["attributes"]["exp"] = 5;
  dm->ApplyCloudSnapshot(otherService);
  assert(dm->GetCloudSnapshot().is_null());
  dm->LoadCloudCache("123");
  assert(dm->GetCloudProfile()["attributes"]["exp"] == 95);
  assert(dm->GetCloudProfile()["online"] == false);
  client->Disconnect();
  assert(!client->Online());
  client->Stop();
  assert(Json::parse(fixture.Get("/stats")->body)["closes"] == 1);
  std::cout << "PASS native WebSocket transport: idle heartbeat, persistent connection, lost-response and 5xx replay, business failure, server push, cache isolation, offline pause, close\n";
}
