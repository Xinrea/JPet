#include "GameManager.hpp"

#include <algorithm>
#include <chrono>

#include "Achievements.hpp"
#include "BuffManager.hpp"
#include "DataManager.hpp"
#include "GameTask.hpp"
#include "LAppPal.hpp"
#include "PanelServer.hpp"

namespace {
using nlohmann::json;
constexpr const char* SaveKey = "game.state";
// The removed cloud client cached its last snapshot under this service address.
constexpr const char* FormerCloudUrl = "https://s.jpet.powerlive.io";
// Sub-minute progress is saved at least this often; changes are saved at once.
constexpr int64_t SaveIntervalMs = 15'000;

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// The save written before gameplay moved to the cloud, kept in separate keys.
json LegacySave(DataManager* data) {
  json attributes = json::object();
  for (const auto* key : {"speed", "endurance", "strength", "will", "intellect", "exp", "buycnt"}) {
    const int value = data->GetWithDefault(std::string("attr.") + key, -1);
    if (value >= 0) attributes[key] = value;
  }
  json result = {{"profile", {{"attributes", attributes}, {"starcnt", data->GetWithDefault("starcnt", 0)},
      {"clothes", {{"current", data->GetWithDefault("clothes.current", 0)},
          {"unlock", {true, data->GetWithDefault("clothes.1.active", 0) == 1, data->GetWithDefault("clothes.2.active", 0) == 1}}}}}},
      {"tasks", json::array()}, {"queue", json::array()}, {"failcount", data->GetWithDefault("buff.failcount", 0)}};
  auto saved = json::parse(data->GetWithDefault("achievements.state", std::string{}), nullptr, false);
  if (saved.is_object()) result["achievements"] = saved;
  saved = json::parse(data->GetWithDefault("task.queue", std::string{}), nullptr, false);
  if (saved.is_object() && saved.contains("entries")) result["queue"] = saved["entries"];
  saved = json::parse(data->GetWithDefault("task.history", std::string{}), nullptr, false);
  if (saved.is_array()) result["history"] = saved;
  const auto now = time(nullptr);
  for (int id = 1; id <= 13; ++id) {
    const auto status = data->TaskStatus(id);
    result["tasks"].push_back({{"id", id}, {"status", status[3]}, {"success", status[2] == 1},
        {"end_time", status[1]}, {"cost_snapshot", status[4]},
        {"elapsed_seconds", status[3] == 1 ? std::max<int64_t>(0, now - status[0]) : 0},
        {"queued", data->GetWithDefault("task." + std::to_string(id) + ".queued", 0) == 1}});
  }
  return result;
}

// The parts of a save whose change is shown to the player. Progress counters
// and achievement metrics are left to the regular save interval.
json Fingerprint(const GameRules::State& state) {
  auto result = GameRules::Save(state);
  result.erase("exp_progress_ms");
  if (result["current"].is_object()) result["current"].erase("elapsed_ms");
  result["achievements"] = state.achievements["unlocked"];
  result["buffs"] = state.buffs;
  result["medal"] = state.medal;
  return result;
}
}  // namespace

GameManager* GameManager::GetInstance() {
  static GameManager instance;
  return &instance;
}
GameManager::~GameManager() { Stop(); }

void GameManager::Load() {
  if (loaded_) return;
  loaded_ = true;
  auto* data = DataManager::GetInstance();
  const auto now = NowMs();
  const auto saved = data->GetWithDefault(SaveKey, std::string{});
  if (!saved.empty()) {
    try {
      state_ = GameRules::Load(json::parse(saved));
      return;
    } catch (const std::exception& e) {
      LAppPal::PrintLog(LogLevel::Error, "[GameManager]Invalid save, starting from older data: %s", e.what());
      data->SetRaw<std::string>(std::string(SaveKey) + ".invalid", saved);
    }
  }
  // First run of the local game: continue from the last cloud save this client
  // received, otherwise from the pre-cloud local save.
  for (const auto& uid : {data->GetWithDefault("uid", std::string{}), data->GetWithDefault("cloud.migration_uid", std::string{})}) {
    if (uid.empty()) continue;
    const auto cached = json::parse(data->GetWithDefault("cloud.cache." + uid + "." + FormerCloudUrl, std::string{}), nullptr, false);
    if (!cached.is_object() || cached.value("uid", std::string{}) != uid || !cached.contains("profile")) continue;
    state_ = GameRules::FromCloudSnapshot(cached, now);
    LAppPal::PrintLog(LogLevel::Info, "[GameManager]Imported the cloud save of %s", uid.c_str());
    Persist(now);
    return;
  }
  const auto bootstrap = json::parse(data->GetWithDefault("cloud.legacy_bootstrap", std::string{}), nullptr, false);
  state_ = GameRules::Create(bootstrap.is_object() ? bootstrap : LegacySave(data), now);
  Persist(now);
}

void GameManager::Persist(int64_t now) {
  DataManager::GetInstance()->SetRaw<std::string>(SaveKey, GameRules::Save(state_).dump());
  savedAt_ = now;
}

void GameManager::Advance(int64_t now) {
  if (!clock_) return;
  if (now > clock_) {
    std::uniform_real_distribution<double> roll(0, 1);
    GameRules::Advance(state_, clock_, std::min(now, clock_ + GameRules::MaxTickGapMs), [&] { return roll(random_); });
  }
  // A clock set backwards credits nothing rather than pausing until it catches up.
  clock_ = now;
}

GameManager::Changes GameManager::Commit(const json& before, int64_t now) {
  Changes changes;
  const auto after = Fingerprint(state_);
  if (after == before) {
    if (now - savedAt_ >= SaveIntervalMs || now < savedAt_) Persist(now);
    return changes;
  }
  Persist(now);
  changes.any = true;
  for (const auto& definition : Achievements::Catalog()) {
    if (after["achievements"].contains(definition.id) && !before["achievements"].contains(definition.id))
      changes.unlocked.push_back({{"id", definition.id}, {"title", definition.title}, {"icon", definition.icon}});
  }
  const auto& history = after["history"];
  if (!history.empty() && (before["history"].empty() || before["history"][0] != history[0])) changes.settled = history[0];
  if (after["clothes"]["current"] != before["clothes"]["current"]) changes.clothes = state_.clothes;
  return changes;
}

void GameManager::Publish(const Changes& changes) {
  if (!changes.any) return;
  auto* panel = PanelServer::GetInstance();
  if (changes.clothes >= 0) pendingClothes_ = changes.clothes;
  if (!changes.unlocked.empty())
    panel->Notify(json{{"type", "ACHIEVEMENT_UNLOCKED"}, {"achievements", changes.unlocked}}.dump());
  if (changes.settled.is_object()) {
    panel->Notify("TASK_COMPLETE");
    GameTask notification;
    notification.Notify(changes.settled.value("success", false) ? L"任务成功，奖励已发放" : L"任务失败，已自动结算",
        LAppPal::StringToWString(changes.settled.value("title", std::string{})), new WinToastEventHandler("TASK_COMPLETE"));
  }
  panel->Notify("UPDATE");
}

void GameManager::Start() {
  if (running_.exchange(true)) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    Load();
    clock_ = NowMs();
  }
  worker_ = std::thread(&GameManager::Run, this);
}

void GameManager::Stop() {
  if (!running_.exchange(false)) return;
  {
    std::lock_guard<std::mutex> lock(waitMutex_);
    wake_.notify_all();
  }
  if (worker_.joinable()) worker_.join();
  std::lock_guard<std::mutex> lock(mutex_);
  const auto now = NowMs();
  Advance(now);
  Persist(now);
  clock_ = 0;
}

void GameManager::Run() {
  while (running_) {
    try {
      Tick();
    } catch (const std::exception& e) {
      LAppPal::PrintLog(LogLevel::Error, "[GameManager]Tick failed: %s", e.what());
    }
    std::unique_lock<std::mutex> lock(waitMutex_);
    wake_.wait_for(lock, std::chrono::seconds(1), [this] { return !running_; });
  }
}

void GameManager::Tick() {
  auto* buffs = BuffManager::GetInstance();
  const auto list = buffs->GetBuffList();
  const auto medal = buffs->MedalLevel();
  Changes changes;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = NowMs();
    const auto before = Fingerprint(state_);
    // Settle the elapsed interval with the bonuses it ran under.
    Advance(now);
    GameRules::UpdateBuffs(state_, list, medal);
    changes = Commit(before, now);
  }
  Publish(changes);
}

std::string GameManager::Command(const json& action) {
  std::string error;
  Changes changes;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    Load();
    const auto now = NowMs();
    const auto before = Fingerprint(state_);
    Advance(now);
    try {
      GameRules::Execute(state_, action, now);
    } catch (const GameRules::Error& e) {
      error = e.what();
    }
    changes = Commit(before, now);
  }
  Publish(changes);
  return error;
}

void GameManager::Touch() {
  Changes changes;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    Load();
    const auto now = NowMs();
    const auto before = Fingerprint(state_);
    GameRules::RecordTouches(state_, 1, now);
    changes = Commit(before, now);
  }
  Publish(changes);
}

json GameManager::Snapshot() {
  std::lock_guard<std::mutex> lock(mutex_);
  Load();
  return GameRules::Snapshot(state_);
}

float GameManager::TaskProgress() {
  std::lock_guard<std::mutex> lock(mutex_);
  Load();
  if (!state_.current || state_.current->durationMs <= 0) return 0;
  // Include time since the last tick so the bar moves smoothly.
  const auto pending = clock_ ? std::clamp<int64_t>(NowMs() - clock_, 0, GameRules::MaxTickGapMs) : 0;
  return std::min(1.0f, static_cast<float>(state_.current->elapsedMs + pending) / static_cast<float>(state_.current->durationMs));
}
