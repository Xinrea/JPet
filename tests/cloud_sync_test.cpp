// Exercise the production HTTP transport and RocksDB snapshot cache without GUI.
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
#include <set>
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

bool waitUntil(const std::function<bool()>& predicate) {
  for (int i = 0; i < 200; ++i) { if (predicate()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(25)); }
  return false;
}
int main(int argc, char** argv) {
  assert(argc == 2);
  LAppDefine::documentPath = LAppPal::StringToWString(argv[1]);
  auto dm = DataManager::GetInstance();
  dm->SetRaw("uid", std::string{"123"}); dm->SetRaw("uname", std::string{"轴伊"}); dm->SetRaw("cookies", std::string{"local-test-cookie"});
  using Json = nlohmann::json;
  Json state = {{"schema", 1}, {"uid", "123"}, {"revision", 1}, {"online", true}, {"lease_remaining_ms", 30000}, {"share", false},
    {"profile", {{"attributes", {{"speed", 2}, {"endurance", 1}, {"strength", 1}, {"will", 3}, {"intellect", 4}, {"exp", 100}, {"buycnt", 0}}},
      {"starcnt", 0}, {"clothes", {{"current", 0}, {"unlock", {true, false, false}}}}, {"expdiff", 2}, {"buffs", {"live"}}, {"exp_progress_seconds", 10}}},
    {"tasks", {{"current", nullptr}, {"queue", Json::array()}, {"list", Json::array()}, {"history", Json::array()}, {"queue_capacity", 2}, {"queue_blocked", false}}},
    {"achievements", {{"total", 50}, {"unlocked", 0}, {"list", Json::array()}}},
    {"save", {{"failcount", 0}, {"achievements", {{"metrics", Json::object()}, {"unlocked", Json::object()}}}}}};
  std::mutex serverMutex;
  std::set<std::string> commands;
  int attempts = 0;
  bool lostResponse = false;
  httplib::Server server;
  server.Post("/v1/:kind", [&](const auto& req, auto& res) {
    std::lock_guard<std::mutex> lock(serverMutex);
    const auto payload = Json::parse(req.body);
    assert(!payload.contains("cookies"));
    const auto kind = req.path_params.at("kind");
    if (kind == "command") {
      ++attempts;
      if (commands.insert(payload["request_id"].template get<std::string>()).second) {
        state["profile"]["attributes"]["speed"] = 3;
        state["profile"]["attributes"]["exp"] = 90;
        state["profile"]["attributes"]["buycnt"] = 1;
      }
      if (!lostResponse) { lostResponse = true; res.status = 503; res.set_content("{\"error\":\"uncertain response\"}", "application/json"); return; }
    }
    state["revision"] = state["revision"].template get<int>() + 1;
    state["online"] = kind != "close";
    res.set_content(Json{{"snapshot", state}}.dump(), "application/json");
  });
  const auto url = CloudGame::ServiceUrl();
  const std::string testOrigin = "http://127.0.0.1:";
  assert(url.rfind(testOrigin, 0) == 0);
  const int port = std::stoi(url.substr(testOrigin.size()));
  assert(server.bind_to_port("127.0.0.1", port));
  setenv("JPET_CLOUD_URL", "https://ignored.invalid/from-environment", 1);
  assert(dm->GetConfig("cloud", "url", std::string{}) == "https://ignored.invalid/from-toml");
  std::thread serving([&] { server.listen_after_bind(); });
  auto client = CloudGame::GetInstance();
  assert(client->Status()["configured"] == true);
  client->Start();
  assert(waitUntil([&] { return client->Online(); }));
  assert(dm->GetCloudProfile()["attributes"]["exp"] == 100);
  auto error = client->Command({{"type", "attr.buy"}, {"attr", "speed"}});
  assert(!error.empty());
  assert(waitUntil([&] { return client->Online() && dm->GetWithDefault("cloud.pending", std::string{}).empty(); }));
  assert(dm->GetCloudProfile()["attributes"]["speed"] == 3);
  assert(dm->GetCloudProfile()["attributes"]["exp"] == 90);
  { std::lock_guard<std::mutex> lock(serverMutex); assert(commands.size() == 1 && attempts >= 2); }
  auto old = state; old["revision"] = 0; old["profile"]["attributes"]["exp"] = 999;
  dm->ApplyCloudSnapshot(old);
  assert(dm->GetAttribute("exp") == 90);
  old["uid"] = "456"; old["revision"] = 100000; dm->ApplyCloudSnapshot(old);
  assert(dm->GetAttribute("exp") == 90);
  dm->SetRaw("uid", std::string{"456"});
  assert(dm->GetCloudSnapshot().is_null());
  assert(dm->GetCloudProfile()["attributes"]["exp"] == 0);
  dm->SetRaw("uid", std::string{"123"});
  auto leased = state; leased["lease_remaining_ms"] = 20;
  dm->ApplyCloudSnapshot(leased);
  std::this_thread::sleep_for(std::chrono::milliseconds(40));
  const auto expired = dm->GetCloudProfile();
  assert(expired["online"] == false && expired["attributes"]["exp"] == 90);
  assert(expired["exp_progress_seconds"].get<double>() >= 10.02);
  setenv("JPET_CLOUD_URL", "https://other.example.com", 1);
  assert(CloudGame::ServiceUrl() == url);
  assert(dm->GetCloudProfile()["attributes"]["exp"] == 90);
  dm->LoadCloudCache("123", "https://other.example.com");
  auto otherService = state; otherService["revision"] = 1; otherService["profile"]["attributes"]["exp"] = 5;
  dm->ApplyCloudSnapshot(otherService);
  assert(dm->GetCloudSnapshot().is_null());
  dm->LoadCloudCache("123");
  assert(dm->GetCloudProfile()["attributes"]["exp"] == 90);
  assert(dm->GetCloudProfile()["online"] == false);
  client->Disconnect();
  assert(!client->Online());
  client->Stop(); server.stop(); serving.join();
  std::cout << "PASS native cloud transport: compiled endpoint, ignored runtime overrides, retry, cache isolation, offline pause\n";
}
