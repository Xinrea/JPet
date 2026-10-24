#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// Progression rules: experience, attributes, stars, tasks, rewards, clothes and
// achievements. Pure functions over State, so they can be tested without the app.
namespace GameRules {
using Json = nlohmann::json;

constexpr int64_t MinuteMs = 60'000;
// Time between two ticks beyond this (system sleep, a stalled process) is not
// credited, so progress only accumulates while JPet is actually running.
constexpr int64_t MaxTickGapMs = 30'000;
constexpr int BaseQueueCapacity = 2;
inline constexpr std::array<int64_t, 4> QueueUpgradeCosts{1, 2, 5, 10};
inline constexpr std::array<const char*, 5> Attributes{"speed", "endurance", "strength", "will", "intellect"};

struct TaskDefinition {
  int id;
  int64_t cost;  // Base duration in seconds.
  const char* title;
  const char* desc;
  std::map<std::string, int64_t> requirements;
  std::map<std::string, int64_t> rewards;
  bool repeatable;
  int clothes = 0;  // Outfit unlocked on success; 0 for none.
  const char* specialTitle = nullptr;
  const char* specialDesc = nullptr;
};
const std::vector<TaskDefinition>& Tasks();
const TaskDefinition* FindTask(int id);

struct RunningTask {
  int id = 0;
  int64_t durationMs = 0, elapsedMs = 0;
  bool queued = false;  // Started automatically from the queue.
  int64_t startedAt = 0;
};
struct QueueEntry {
  int64_t entryId = 0;
  int taskId = 0;
};
struct State {
  std::map<std::string, int64_t> attributes;  // Five attributes plus exp and buycnt.
  int64_t stars = 0;
  int clothes = 0;
  std::array<bool, 3> unlockedClothes{true, false, false};
  int64_t failcount = 0;
  Json achievements;
  std::vector<int> archived;  // Completed one-off tasks.
  std::optional<RunningTask> current;
  std::vector<QueueEntry> queue;
  int64_t nextEntry = 1;
  int queueUpgrades = 0;
  Json history = Json::array();  // Latest ten settlements, newest first.
  int64_t expProgressMs = 0;
  // External bonuses observed by BuffManager; not saved.
  std::vector<std::string> buffs;
  int64_t medal = 0;
};

// Rejected commands. The message is shown to the user.
class Error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// A new game, importing a pre-cloud local save when given one.
State Create(const Json& legacy, int64_t nowMs);
// Imports the last snapshot the removed cloud service sent to this client.
State FromCloudSnapshot(const Json& snapshot, int64_t nowMs);
Json Save(const State& state);
State Load(const Json& saved);

void UpdateBuffs(State& state, const std::vector<std::string>& buffs, int64_t medal);
// Runs the game from `fromMs` to `toMs`, settling minutes and tasks in order.
// `random` returns a value in [0, 1) for task success rolls.
void Advance(State& state, int64_t fromMs, int64_t toMs, const std::function<double()>& random);
// Applies one player action; throws Error without changing the state when rejected.
void Execute(State& state, const Json& action, int64_t nowMs);
void RecordTouches(State& state, int64_t count, int64_t nowMs);
// Panel and voice-tool view: {profile, tasks, achievements, save}.
Json Snapshot(const State& state);

int64_t ExpDiff(const State& state);
int64_t TaskCost(const State& state, const TaskDefinition& task);
double SuccessRate(const State& state, const TaskDefinition& task);
int QueueCapacity(const State& state);
}  // namespace GameRules
