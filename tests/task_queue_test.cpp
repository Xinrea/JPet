// Exercise the production task controller and RocksDB persistence without GUI,
// account services or the minute timer. App service adapters are below.
#include "DataManager.hpp"
#include "PanelServer.hpp"
#include "Platform.hpp"
#include <cassert>
#include <codecvt>
#include <iostream>
#include <locale>
#include <thread>
#include <atomic>

namespace LAppDefine {
std::wstring documentPath;
const Csm::csmBool DebugLogEnable = false;
}
void LAppPal::PrintLog(LogLevel, const char*, ...) {}
void LAppPal::PrintLog(const char*, ...) {}
std::wstring LAppPal::StringToWString(const std::string& text) {
  return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.from_bytes(text);
}
std::string LAppPal::WStringToString(const std::wstring& text) {
  return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.to_bytes(text);
}
double LAppPal::EaseOut(int value) {
  double p = std::clamp(value, 0, 100) / 100.0;
  return 100 * (1 - (1 - p) * (1 - p));
}
void PanelServer::Notify(const std::string&) {}
void Platform::Notify(const std::wstring&, const std::wstring&, const std::string&) {}
DataManager::DataManager() {
  std::filesystem::create_directories(std::filesystem::path(LAppDefine::documentPath));
  gameData = std::make_shared<GameData>(LAppDefine::documentPath + L"/unused.dat");
  assert(gameData->Initialized());
}
DataManager* DataManager::GetInstance() { static DataManager instance; return &instance; }
void DataManager::Save() {}
void DataManager::PostProcess(const std::string&, int) {}
int DataManager::GetWithDefault(const std::string& key, int fallback) {
  int value = fallback; gameData->Get(key, value); return value;
}
std::string DataManager::GetWithDefault(const std::string& key, const std::string& fallback) {
  std::string value = fallback; gameData->Get(key, value); return value;
}
int DataManager::GetAttribute(const std::string& key) { return GetWithDefault("attr." + key, 0); }
int DataManager::GetAttrLimit() { return 100 + 10 * GetWithDefault("starcnt", 0); }
int DataManager::CurrentExpDiff() { return 60; }
std::vector<int> DataManager::TaskStatus(int id) {
  std::vector<int> result;
  for (auto key : {"start_time", "end_time", "success", "status", "cost_snapshot"})
    result.push_back(GetWithDefault("task." + std::to_string(id) + "." + key, 0));
  return result;
}
void DataManager::DumpTask(int id, int start, int end, int success, int status, int cost) {
  const auto prefix = "task." + std::to_string(id) + ".";
  gameData->UpdateBatch({{prefix + "start_time", start}, {prefix + "end_time", end},
      {prefix + "success", success}, {prefix + "status", status}, {prefix + "cost_snapshot", cost}});
}

void attributes(DataManager* dm, int value) {
  for (auto key : {"speed", "endurance", "strength", "will", "intellect"})
    dm->SetRaw(std::string{"attr."} + key, value);
}
void savedTask(DataManager* dm, int id, bool success, TStatus status) {
  dm->DumpTask(id, 1000, 0, success, static_cast<int>(status), 10);
}
int64_t entry(DataManager* dm, int index) {
  return dm->GetTaskState()["queue"][index]["entry_id"].get<int64_t>();
}

int main(int argc, char** argv) {
  assert(argc >= 3);
  const std::string scenario = argv[1];
  LAppDefine::documentPath = LAppPal::StringToWString(argv[2]);
  auto dm = DataManager::GetInstance();
  if (scenario == "preview") {
    assert(argc == 4);
    attributes(dm, 50);
    savedTask(dm, 2, true, TStatus::WAIT_SETTLE);
    dm->TickTasks(time(nullptr));
    dm->StartTask(6);
    dm->QueueTask(4);
    dm->QueueTask(5);
    httplib::Server preview;
    preview.set_mount_point("/", argv[3]);
    preview.Get("/api/task", [&](const auto&, auto& res) {
      res.set_content(dm->GetTaskState().dump(), "application/json");
    });
    preview.Get("/api/profile", [&](const auto&, auto& res) {
      nlohmann::json profile = {{"attributes", {{"speed", dm->GetAttribute("speed")},
          {"endurance", dm->GetAttribute("endurance")}, {"strength", dm->GetAttribute("strength")},
          {"will", dm->GetAttribute("will")}, {"intellect", dm->GetAttribute("intellect")},
          {"exp", dm->GetAttribute("exp")}, {"buycnt", 0}}},
          {"clothes", {{"current", 0}, {"unlock", {true, false, false}}}},
          {"expdiff", 60}, {"starcnt", dm->GetWithDefault("starcnt", 0)},
          {"buffs", nlohmann::json::array()}};
      res.set_content(profile.dump(), "application/json");
    });
    preview.Get("/api/achievements", [&](const auto&, auto& res) {
      res.set_content(dm->GetAchievementState().dump(), "application/json");
    });
    preview.Get("/api/config/shortcut", [](const auto&, auto& res) { res.set_content("[]", "application/json"); });
    preview.Get("/api/config/account", [](const auto&, auto& res) { res.set_content("{\"login\":false}", "application/json"); });
    preview.Get("/api/sse", [](const auto&, auto& res) {
      res.set_chunked_content_provider("text/event-stream", [](size_t, httplib::DataSink& sink) {
        if (!sink.is_writable()) return false;
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return sink.write("data: UPDATE\n\n", 14);
      });
    });
    preview.Get("/api/.*", [](const auto&, auto& res) { res.set_content("{}", "application/json"); });
    auto respond = [&](auto& res, const std::string& error) {
      res.status = error.empty() ? 200 : 409;
      res.set_content(error.empty() ? dm->GetTaskState().dump() : nlohmann::json{{"error", error}}.dump(), "application/json");
    };
    preview.Post("/api/task/:id/start", [&](const auto& req, auto& res) { respond(res, dm->StartTask(std::stoi(req.path_params.at("id")))); });
    preview.Post("/api/task/:id/queue", [&](const auto& req, auto& res) { respond(res, dm->QueueTask(std::stoi(req.path_params.at("id")))); });
    preview.Post("/api/task/:id/cancel", [&](const auto& req, auto& res) { respond(res, dm->CancelTask(std::stoi(req.path_params.at("id")))); });
    preview.Delete("/api/task/queue/:entryId", [&](const auto& req, auto& res) { respond(res, dm->RemoveQueuedTask(std::stoll(req.path_params.at("entryId")))); });
    preview.Post("/api/task/queue/:entryId/move", [&](const auto& req, auto& res) {
      respond(res, dm->MoveQueuedTask(std::stoll(req.path_params.at("entryId")), nlohmann::json::parse(req.body)["direction"].template get<int>()));
    });
    preview.Post("/api/.*", [](const auto&, auto& res) { res.status = 204; });
    std::cout << "Task preview: http://127.0.0.1:18765" << std::endl;
    assert(preview.listen("127.0.0.1", 18765));
  } else if (scenario == "settle") {
    attributes(dm, 10);
    savedTask(dm, 2, true, TStatus::WAIT_SETTLE); // Migrate an old unclaimed reward.
    assert(dm->QueueTask(6, 1010).empty());
    assert(dm->QueueTask(4, 1010).empty());
    dm->TickTasks(1010);
    assert(dm->GetAttribute("speed") == 11);
    assert(dm->GetAttribute("endurance") == 11);
    assert(dm->GetAttribute("strength") == 11);
    auto state = dm->GetTaskState();
    assert(state["current"]["id"] == 6 && state["queue"][0]["id"] == 4);
    assert(state["history"].size() == 1);
    assert(state["current"]["cost"] == 6271); // Recalculate speed after rewards.
    dm->TickTasks(1010);
    assert(dm->GetAttribute("speed") == 11); // Never grant an old reward twice.
    assert(dm->GetTaskState()["history"].size() == 1);
  } else if (scenario == "restart") {
    auto state = dm->GetTaskState();
    assert(state["current"]["id"] == 6 && state["queue"][0]["id"] == 4);
    assert(state["history"].size() == 1 && dm->GetAttribute("speed") == 11);
    dm->TickTasks(1010);
    assert(dm->GetAttribute("speed") == 11);
    assert(!dm->RemoveQueuedTask(1).empty()); // First ticket is running now.
    assert(dm->GetTaskState()["queue"][0]["id"] == 4);
  } else if (scenario == "fifo") {
    attributes(dm, 50);
    assert(dm->StartTask(2, 1000).empty());
    assert(dm->QueueTask(6, 1000).empty());
    assert(dm->QueueTask(4, 1000).empty());
    assert(!dm->QueueTask(5, 1000).empty());
    const auto six = entry(dm, 0), four = entry(dm, 1);
    assert(dm->MoveQueuedTask(four, -1).empty());
    assert(dm->GetTaskState()["queue"][0]["id"] == 4);
    assert(dm->RemoveQueuedTask(six).empty());
    assert(dm->CancelTask(2, 1001).empty());
    assert(dm->GetTaskState()["current"]["id"] == 4);
    assert(dm->GetTaskState()["queue"].empty());
    dm->TickTasks(100000);
    assert(dm->GetTaskState()["current"].is_null()); // Queue drained; no repeat.
    assert(dm->GetTaskState()["history"].size() == 1);
  } else if (scenario == "duplicates") {
    attributes(dm, 50);
    assert(dm->StartTask(2, 1000).empty());
    assert(dm->QueueTask(2, 1000).empty());
    assert(dm->QueueTask(2, 1000).empty());
    assert(!dm->QueueTask(2, 1000).empty());
    for (int time : {10000, 20000, 30000}) dm->TickTasks(time);
    auto state = dm->GetTaskState();
    assert(state["current"].is_null() && state["queue"].empty());
    assert(state["history"].size() == 3);
    int wins = 0;
    for (auto& task : state["history"]) wins += task["success"].get<bool>();
    assert(dm->GetAttribute("speed") == 50 + wins);
  } else if (scenario == "failure") {
    attributes(dm, 100);
    savedTask(dm, 13, false, TStatus::RUNNING);
    assert(dm->QueueTask(2, 1000).empty());
    dm->SetRaw("starcnt", 14); // Guaranteed failure, including will correction.
    dm->TickTasks(1010);
    auto state = dm->GetTaskState();
    assert(state["current"].is_null() && state["queue_blocked"] == true);
    assert(state["history"].size() == 1 && state["history"][0]["success"] == false);
    assert(dm->GetWithDefault("buff.failcount", 0) == 1);
    assert(dm->GetAttribute("exp") == 0);
    dm->SetRaw("starcnt", 0);
    dm->TickTasks(1011);
    assert(dm->GetTaskState()["current"]["id"] == 2);
  } else if (scenario == "oneoff") {
    attributes(dm, 50);
    savedTask(dm, 8, true, TStatus::WAIT_SETTLE);
    assert(!dm->QueueTask(8, 1010).empty());
    assert(dm->QueueTask(12, 1010).empty());
    assert(!dm->QueueTask(12, 1010).empty());
    dm->TickTasks(1010);
    assert(dm->GetWithDefault("clothes.1.active", 0) == 1);
    assert(!dm->QueueTask(8, 1010).empty());
    assert(!dm->QueueTask(12, 1010).empty());
    assert(dm->GetTaskState()["current"]["id"] == 12);
  } else if (scenario == "blocked") {
    attributes(dm, 10);
    assert(dm->QueueTask(13, 1000).empty());
    assert(dm->GetTaskState()["queue_blocked"] == true);
    assert(dm->QueueTask(6, 1000).empty());
    const auto impossible = entry(dm, 0), six = entry(dm, 1);
    assert(dm->MoveQueuedTask(six, -1).empty());
    assert(dm->GetTaskState()["current"]["id"] == 6);
    assert(dm->RemoveQueuedTask(impossible).empty());
    assert(dm->GetTaskState()["queue"].empty());
  } else if (scenario == "capacity") {
    attributes(dm, 50);
    assert(dm->TaskQueueCapacity() == 2);
    assert(dm->StartTask(2, 1000).empty());
    std::atomic_int admitted{0};
    std::vector<std::thread> requests;
    for (int i = 0; i < 10; ++i) requests.emplace_back([&] {
      if (dm->QueueTask(6, 1000).empty()) ++admitted;
    });
    for (auto& request : requests) request.join();
    assert(admitted == 2 && dm->GetTaskState()["queue"].size() == 2);
    dm->SetRaw("starcnt", 3);
    assert(dm->TaskQueueCapacity() == 5);
    for (int i = 0; i < 3; ++i) assert(dm->QueueTask(6, 1000).empty());
    assert(!dm->QueueTask(6, 1000).empty());
  } else if (scenario == "overflow") {
    attributes(dm, 100);
    savedTask(dm, 2, true, TStatus::WAIT_SETTLE);
    dm->TickTasks(1010);
    assert(dm->GetAttribute("speed") == 100);
    assert(dm->GetAttribute("exp") == 79500);
    assert(dm->GetTaskState()["history"][0]["rewards"]["exp"] == 79500);
    auto state = dm->GetTaskState();
    assert(state["current"].is_null());
    assert(state["history"].size() == 1);
    dm->TickTasks(1011);
    assert(dm->GetAttribute("exp") == 79500);
  } else if (scenario == "bottle") {
    attributes(dm, 10);
    savedTask(dm, 1, true, TStatus::WAIT_SETTLE);
    dm->TickTasks(1010);
    assert(dm->GetAttribute("exp") == 600);
    assert(dm->GetTaskState()["history"][0]["rewards"]["exp"] == 600);
  } else if (scenario == "achievements") {
    attributes(dm, 50);
    savedTask(dm, 1, true, TStatus::WAIT_SETTLE);
    savedTask(dm, 2, true, TStatus::WAIT_SETTLE);
    savedTask(dm, 8, true, TStatus::WAIT_SETTLE);
    dm->SetRaw("task.2.queued", 1);
    dm->TickTasks(1800000000);
    auto state = Achievements::Parse(dm->GetWithDefault("achievements.state", std::string{}));
    assert(state["metrics"]["successes"] == 3);
    assert(state["metrics"]["queued_successes"] == 1);
    assert(state["metrics"]["variety"] == 3);
    assert(state["unlocked"]["task_2"] == 1800000000);
    assert(state["unlocked"]["dress"] == 1800000000);
    assert(state["unlocked"]["balanced_50"] == 1800000000);
    dm->TickTasks(1800000001);
    assert(dm->GetAchievementState()["total"] == 50);
    assert(Achievements::Parse(dm->GetWithDefault("achievements.state", std::string{})) == state);
  } else if (scenario == "achievement_restart") {
    auto before = dm->GetWithDefault("achievements.state", std::string{});
    auto state = dm->GetAchievementState();
    dm->TickTasks(1800000002);
    assert(dm->GetWithDefault("achievements.state", std::string{}) == before);
    assert(state["total"] == 50);
    // Attribute spending must not remove a previous achievement or its progress.
    attributes(dm, 0);
    state = dm->GetAchievementState();
    for (auto& item : state["list"]) {
      if (item["id"] == "balanced_50") {
        assert(item["unlocked"] == true && item["progress"] == 50);
        assert(item["unlocked_at"] == 1800000000);
      }
    }
  } else if (scenario == "achievement_migrate") {
    savedTask(dm, 13, true, TStatus::ARCHIVED);
    savedTask(dm, 2, true, TStatus::IDLE);
    dm->SetRaw("task.history", std::string{"[{\"id\":2,\"success\":true},{\"id\":2,\"success\":true},{\"id\":3,\"success\":false}]"});
    dm->SetRaw("clothes.1.active", 1);
    dm->SetRaw("clothes.2.active", 1);
    dm->SetRaw("starcnt", 3);
    dm->GetAchievementState();
    auto state = Achievements::Parse(dm->GetWithDefault("achievements.state", std::string{}));
    assert(state["metrics"]["successes"] == 3);
    assert(state["metrics"]["task.2"] == 2);
    assert(state["metrics"]["failures"] == 1);
    assert(state["unlocked"].contains("task_13"));
    assert(state["unlocked"].contains("wardrobe"));
    assert(state["unlocked"].contains("star_3"));
    assert(!state["unlocked"].contains("hello"));
  } else if (scenario == "achievement_events") {
    for (int i = 0; i < 60; ++i) dm->RecordAchievementEvent("minute", 1800000000 + i * 60);
    std::vector<std::thread> touches;
    for (int i = 0; i < 10; ++i) touches.emplace_back([&] {
      for (int j = 0; j < 20; ++j) dm->RecordAchievementEvent("touch", 1800000000);
    });
    for (auto& thread : touches) thread.join();
    auto state = Achievements::Parse(dm->GetWithDefault("achievements.state", std::string{}));
    assert(state["metrics"]["minutes"] == 60);
    assert(state["metrics"]["days"] == 1);
    assert(state["metrics"]["touches"] == 200);
    assert(state["unlocked"].contains("hour") && state["unlocked"].contains("touch_200"));
  } else if (scenario == "corrupt") {
    attributes(dm, 10);
    dm->SetRaw("task.queue", std::string{"invalid JSON"});
    assert(dm->GetTaskState()["queue"].empty());
    assert(dm->QueueTask(6, 1000).empty());
    assert(dm->GetTaskState()["current"]["id"] == 6);
  } else return 1;
  std::cout << "PASS " << scenario << '\n';
}
