#include "GameRules.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

#include "Achievements.hpp"

namespace GameRules {
namespace {
constexpr int64_t Max = 99'999'999;
const std::map<std::string, int64_t> DefaultAttributes = {
    {"speed", 2}, {"endurance", 1}, {"strength", 1}, {"will", 3}, {"intellect", 4}, {"exp", 0}, {"buycnt", 0}};

const Json& Field(const Json& object, const char* key) {
  static const Json missing;
  if (!object.is_object()) return missing;
  const auto it = object.find(key);
  return it == object.end() ? missing : *it;
}
// Non-negative whole numbers from untrusted saves, like the former server's number().
int64_t Number(const Json& value, int64_t fallback = 0, int64_t max = Max) {
  if (!value.is_number()) return fallback;
  const double v = value.get<double>();
  if (!std::isfinite(v)) return fallback;
  return static_cast<int64_t>(std::min<double>(static_cast<double>(max), std::max(0.0, std::floor(v))));
}
int64_t Milliseconds(const Json& seconds) {
  if (!seconds.is_number()) return 0;
  const double v = seconds.get<double>();
  return std::isfinite(v) && v > 0 ? std::llround(std::min(v, 1e9) * 1000) : 0;
}
std::optional<int64_t> Integer(const Json& action, const char* key) {
  const auto& value = Field(action, key);
  if (!value.is_number_integer()) return std::nullopt;
  return value.is_number_unsigned() ? static_cast<int64_t>(std::min<uint64_t>(value.get<uint64_t>(), INT64_MAX)) : value.get<int64_t>();
}
Json Objects(const Json& value) {
  Json result = Json::array();
  if (value.is_array()) for (const auto& item : value) if (item.is_object()) result.push_back(item);
  return result;
}

bool IsAttribute(const std::string& key) {
  return std::any_of(Attributes.begin(), Attributes.end(), [&](const char* name) { return key == name; });
}
int64_t Attribute(const State& state, const std::string& key) {
  const auto it = state.attributes.find(key);
  return it == state.attributes.end() ? 0 : it->second;
}
int64_t AttributeLimit(const State& state) { return 100 + state.stars * 10; }
bool Archived(const State& state, int id) {
  return std::find(state.archived.begin(), state.archived.end(), id) != state.archived.end();
}
bool Scheduled(const State& state, int id) {
  return (state.current && state.current->id == id) ||
      std::any_of(state.queue.begin(), state.queue.end(), [&](const QueueEntry& entry) { return entry.taskId == id; });
}
std::optional<int64_t> QueueUpgradeCost(const State& state) {
  if (state.queueUpgrades >= static_cast<int>(QueueUpgradeCosts.size())) return std::nullopt;
  return QueueUpgradeCosts[state.queueUpgrades];
}
int64_t BuyCost(const State& state) {
  const auto buys = Attribute(state, "buycnt");
  return buys < 25 ? static_cast<int64_t>(std::ceil(10 * std::pow(1.41, static_cast<double>(buys)))) : 53000;
}
int64_t RefundGain(const State& state) {
  const auto buys = Attribute(state, "buycnt");
  const int64_t paid = buys < 26 ? static_cast<int64_t>(std::ceil(10 * std::pow(1.41, static_cast<double>(std::max<int64_t>(0, buys - 1))))) : 53000;
  return paid / 2;
}
bool StarAvailable(const State& state) {
  return std::all_of(Attributes.begin(), Attributes.end(), [&](const char* key) { return Attribute(state, key) >= 53; });
}
double EaseOut(double x) {
  const double p = std::min(100.0, std::max(0.0, x)) / 100;
  return 1 - (1 - p) * (1 - p);
}

// Attributes are capped by stars; overflow is converted into experience.
void AddAttribute(State& state, const std::string& key, int64_t delta) {
  const int64_t next = std::min(Max, std::max<int64_t>(0, Attribute(state, key) + delta));
  if (!IsAttribute(key)) {
    state.attributes[key] = next;
    return;
  }
  const auto limit = AttributeLimit(state);
  if (next > limit) AddAttribute(state, "exp", (next - limit) * 26500);
  state.attributes[key] = std::min(limit, next);
}
void EnforceLimits(State& state) {
  for (const auto* key : Attributes) AddAttribute(state, key, 0);
}

void Observe(State& state, int64_t now) {
  std::map<std::string, int> values;
  int64_t balanced = Max;
  for (const auto* key : Attributes) {
    values[key] = static_cast<int>(Attribute(state, key));
    balanced = std::min(balanced, Attribute(state, key));
  }
  values["balanced"] = static_cast<int>(balanced);
  values["exp"] = static_cast<int>(Attribute(state, "exp"));
  values["stars"] = static_cast<int>(std::min<int64_t>(state.stars, Max));
  values["dress"] = state.unlockedClothes[1];
  values["winter"] = state.unlockedClothes[2];
  values["clothes"] = static_cast<int>(std::count(state.unlockedClothes.begin(), state.unlockedClothes.end(), true));
  Achievements::Observe(state.achievements, values);
  Achievements::Evaluate(state.achievements, now / 1000);
}

// Achievement days use the calendar date in UTC+8.
std::string DateUtc8(int64_t ms) {
  const int64_t shifted = ms + 8 * 3'600'000;
  int64_t days = shifted / 86'400'000 - (shifted % 86'400'000 < 0);
  // Civil date from days since 1970-01-01 (H. Hinnant's algorithm).
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const int64_t doe = days - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  const int64_t day = doy - (153 * mp + 2) / 5 + 1;
  const int64_t month = mp < 10 ? mp + 3 : mp - 9;
  const int64_t year = yoe + era * 400 + (month <= 2);
  char text[16];
  std::snprintf(text, sizeof(text), "%04lld-%02lld-%02lld", static_cast<long long>(year),
                static_cast<long long>(month), static_cast<long long>(day));
  return text;
}

Json DescribeTask(const State& state, const TaskDefinition& task) {
  Json result = {{"id", task.id}, {"title", task.title}, {"desc", task.desc},
      {"requirements", task.requirements}, {"repeatable", task.repeatable},
      {"rewards", task.id == 1 ? Json{{"exp", 10 * ExpDiff(state)}} : Json(task.rewards)},
      {"cost", TaskCost(state, task)}, {"rate", SuccessRate(state, task) * 100},
      {"status", Archived(state, task.id) ? 3 : 0}, {"start_time", 0}, {"end_time", 0}, {"success", false}};
  if (task.specialTitle) result["special"] = {{"title", task.specialTitle}, {"desc", task.specialDesc}};
  return result;
}

void StartNext(State& state, int64_t now) {
  if (state.current || state.queue.empty()) return;
  const auto* task = FindTask(state.queue.front().taskId);
  if (!task) {
    state.queue.erase(state.queue.begin());
    return StartNext(state, now);
  }
  // A queue head that cannot succeed waits until attributes or order change.
  if (SuccessRate(state, *task) == 0) return;
  state.queue.erase(state.queue.begin());
  state.current = RunningTask{task->id, TaskCost(state, *task) * 1000, 0, true, now};
}

void Settle(State& state, const TaskDefinition& task, bool success, bool queued, int64_t now) {
  const auto previous = state.attributes;
  if (success) {
    for (const auto& [key, value] : task.rewards) AddAttribute(state, key, value);
    if (task.id == 1) AddAttribute(state, "exp", 10 * ExpDiff(state));
    if (task.clothes) state.unlockedClothes[task.clothes] = true;
    state.failcount = 0;
    if (!task.repeatable && !Archived(state, task.id)) state.archived.push_back(task.id);
  } else {
    ++state.failcount;
  }
  Achievements::Task(state.achievements, task.id, success, queued);
  Json rewards = Json::object();
  for (const auto& [key, value] : state.attributes) {
    const auto before = previous.count(key) ? previous.at(key) : 0;
    if (value > before) rewards[key] = value - before;
  }
  auto record = DescribeTask(state, task);
  record["success"] = success;
  record["rewards"] = rewards;
  record["end_time"] = now / 1000;
  if (!success) record.erase("special");
  state.history.insert(state.history.begin(), record);
  if (state.history.size() > 10) state.history.erase(state.history.begin() + 10, state.history.end());
  Observe(state, now);
}

State Blank() {
  State state;
  state.attributes = DefaultAttributes;
  state.achievements = Achievements::EmptyState();
  return state;
}
void ReadClothes(State& state, const Json& clothes) {
  const auto& unlock = Field(clothes, "unlock");
  for (size_t i = 1; i < state.unlockedClothes.size(); ++i)
    state.unlockedClothes[i] = unlock.is_array() && unlock.size() > i && unlock[i] == true;
  const auto current = static_cast<int>(Number(Field(clothes, "current"), 0, 2));
  state.clothes = state.unlockedClothes[current] ? current : 0;
}
void ReadAttributes(State& state, const Json& attributes) {
  for (const auto& [key, fallback] : DefaultAttributes) state.attributes[key] = Number(Field(attributes, key.c_str()), fallback);
}
void ReadAchievements(State& state, const Json& achievements) {
  state.achievements = achievements.is_object() ? Achievements::Parse(achievements.dump()) : Achievements::EmptyState();
}
// Adds a queue entry unless the task is unknown, finished or a duplicate one-off.
void Enqueue(State& state, int id, int64_t entryId = 0) {
  const auto* task = FindTask(id);
  if (!task || Archived(state, id) || (!task->repeatable && Scheduled(state, id))) return;
  if (entryId <= 0) entryId = state.nextEntry;
  state.queue.push_back({entryId, id});
  state.nextEntry = std::max(state.nextEntry, entryId + 1);
}
}  // namespace

const std::vector<TaskDefinition>& Tasks() {
  // Stable IDs are part of saves and achievement metrics.
  static const std::vector<TaskDefinition> tasks = {
      {1, 300, "拧瓶盖", "「我真的能办到吗...」轴伊呆呆地凝望着饮料瓶", {{"intellect", 7}, {"strength", 7}}, {{"exp", 0}}, true},
      {2, 3600, "跑步 800m", "跑步训练！这是真实存在的吗？", {{"endurance", 8}, {"strength", 5}},
       {{"endurance", 1}, {"strength", 1}, {"speed", 1}}, true},
      {3, 7200, "日常直播", "「哈喽哈喽晚上好！」", {{"will", 5}, {"intellect", 3}}, {{"endurance", 3}, {"intellect", 2}}, true},
      {4, 7200, "健身环直播", "*轴伊从尘封的抽屉中刨出了健身环", {{"endurance", 10}, {"strength", 10}},
       {{"endurance", 3}, {"strength", 2}, {"will", 2}}, true},
      {5, 7200, "困困夜行电台直播", "闲聊与读故事 time", {{"endurance", 3}, {"will", 5}}, {{"will", 3}, {"intellect", 2}}, true},
      {6, 7200, "游戏直播", "「是采集卡延迟，真的。」", {{"speed", 3}, {"will", 5}}, {{"speed", 2}, {"intellect", 3}}, true},
      {7, 7200, "歌回直播", "「屋内的湿气 像储存爱你的记忆~」", {{"will", 5}, {"intellect", 3}}, {{"will", 4}}, true},
      {8, 7200, "新衣装发布回 - 礼服", "新衣装发布啦~",
       {{"speed", 10}, {"endurance", 10}, {"strength", 10}, {"will", 10}, {"intellect", 10}}, {}, false, 1, "礼服", "解锁新衣装 - 礼服"},
      {9, 14400, "舞蹈课", "*轴伊正在等车中", {{"endurance", 20}, {"strength", 15}},
       {{"speed", 4}, {"endurance", 6}, {"strength", 5}}, true},
      {10, 28800, "旅游", "*轴伊正在收拾东西出门", {{"speed", 25}, {"endurance", 25}, {"strength", 25}},
       {{"speed", 8}, {"endurance", 8}, {"strength", 6}, {"will", 6}, {"intellect", 8}}, false},
      {11, 14400, "策划联动直播", "「Work work」", {{"will", 30}, {"intellect", 30}}, {{"will", 8}, {"intellect", 8}}, true},
      {12, 7200, "新衣装发布回 - 冬装", "冬装发布啦~",
       {{"speed", 50}, {"endurance", 50}, {"strength", 50}, {"will", 50}, {"intellect", 50}}, {}, false, 2, "冬装", "解锁新衣装 - 冬装"},
      {13, 86400, "全能之主", "五角星战士轴伊在此",
       {{"speed", 100}, {"endurance", 100}, {"strength", 100}, {"will", 100}, {"intellect", 100}}, {{"exp", 500000}}, false},
  };
  return tasks;
}
const TaskDefinition* FindTask(int id) {
  const auto& tasks = Tasks();
  const auto it = std::find_if(tasks.begin(), tasks.end(), [&](const TaskDefinition& task) { return task.id == id; });
  return it == tasks.end() ? nullptr : &*it;
}

int64_t ExpDiff(const State& state) {
  double exp = 1 + std::ceil(499 * EaseOut(static_cast<double>(Attribute(state, "intellect") + state.medal / 3 - 4)));
  const std::pair<const char*, double> factors[] = {{"live", 2}, {"dynamic", 1.5}, {"guard", 1.25}, {"monday", 1.25}, {"birthday", 3.5}};
  for (const auto& [buff, factor] : factors)
    if (std::find(state.buffs.begin(), state.buffs.end(), buff) != state.buffs.end()) exp *= factor;
  if (state.failcount >= 2) exp *= 1.25;
  return std::min<int64_t>(Max, static_cast<int64_t>(std::floor(exp * (1 + 0.1 * static_cast<double>(state.stars)))));
}
int64_t TaskCost(const State& state, const TaskDefinition& task) {
  return std::max<int64_t>(1, static_cast<int64_t>(std::floor(
      static_cast<double>(task.cost) * (1 - 0.75 * EaseOut(static_cast<double>(Attribute(state, "speed") - 2))))));
}
double SuccessRate(const State& state, const TaskDefinition& task) {
  int64_t lack = 0;
  for (const auto& [key, value] : task.requirements) lack += std::max<int64_t>(0, value - Attribute(state, key));
  lack = lack * 12 + 120 + 20 * state.stars;
  if (lack >= 400) return 0;
  return static_cast<double>(400 - std::max<int64_t>(20, lack - Attribute(state, "will"))) / 400;
}
int QueueCapacity(const State& state) { return BaseQueueCapacity + state.queueUpgrades; }

State Create(const Json& legacy, int64_t nowMs) {
  auto state = Blank();
  const auto& profile = Field(legacy, "profile");
  ReadAttributes(state, Field(profile, "attributes"));
  ReadClothes(state, Field(profile, "clothes"));
  state.stars = Number(Field(profile, "starcnt"));
  state.failcount = Number(Field(legacy, "failcount"));
  const bool hasAchievements = !Field(legacy, "achievements").is_null();
  ReadAchievements(state, Field(legacy, "achievements"));
  EnforceLimits(state);
  std::set<int> completed;
  for (const auto& record : Objects(Field(legacy, "tasks"))) {
    const auto& id = Field(record, "id");
    const auto* task = id.is_number_integer() ? FindTask(id.get<int>()) : nullptr;
    if (!task) continue;
    const auto status = Field(record, "status").is_number_integer() ? Field(record, "status").get<int>() : -1;
    const bool success = Field(record, "success") == true, queued = Field(record, "queued") == true;
    if (status == 3 && !task->repeatable) state.archived.push_back(task->id);
    if (status == 1 && !state.current) {
      const auto seconds = Number(Field(record, "cost_snapshot"));
      const auto duration = (seconds ? seconds : TaskCost(state, *task)) * 1000;
      state.current = RunningTask{task->id, duration, std::min(duration, Number(Field(record, "elapsed_seconds")) * 1000), queued, nowMs};
    }
    // A reward that was due but never claimed is settled once.
    if (status == 2) Settle(state, *task, success, queued, nowMs);
    // Old saves may have no achievement counters. Import only surviving evidence.
    if (!hasAchievements && (status == 3 || (status == 0 && success && Number(Field(record, "end_time")) > 0))) completed.insert(task->id);
  }
  const auto queue = Objects(Field(legacy, "queue"));
  // Keep tasks queued under the old star-based limit; new slots require upgrades.
  for (size_t i = 0; i < queue.size() && i < static_cast<size_t>(BaseQueueCapacity + state.stars); ++i)
    Enqueue(state, static_cast<int>(Number(Field(queue[i], "task_id"))));
  auto history = Objects(Field(legacy, "history"));
  if (history.size() > 10) history.erase(history.begin() + 10, history.end());
  for (const auto& record : history) if (state.history.size() < 10) state.history.push_back(record);
  if (!hasAchievements) {
    for (const auto& record : history) {
      const auto id = Number(Field(record, "id"));
      if (!FindTask(static_cast<int>(id))) continue;
      if (Field(record, "success") == true) Achievements::Increment(state.achievements, "task." + std::to_string(id));
      else Achievements::Increment(state.achievements, "failures");
    }
    auto& metrics = state.achievements["metrics"];
    for (const auto id : completed) {
      const auto key = "task." + std::to_string(id);
      if (!Achievements::Number(metrics, key)) metrics[key] = 1;
    }
    int64_t successes = 0;
    for (const auto& task : Tasks()) successes += Achievements::Number(metrics, "task." + std::to_string(task.id));
    metrics["successes"] = successes;
  }
  Observe(state, nowMs);
  return state;
}

State FromCloudSnapshot(const Json& snapshot, int64_t nowMs) {
  auto state = Blank();
  const auto& profile = Field(snapshot, "profile");
  const auto& tasks = Field(snapshot, "tasks");
  const auto& save = Field(snapshot, "save");
  ReadAttributes(state, Field(profile, "attributes"));
  ReadClothes(state, Field(profile, "clothes"));
  state.stars = Number(Field(profile, "starcnt"));
  state.failcount = Number(Field(save, "failcount"));
  ReadAchievements(state, Field(save, "achievements"));
  state.expProgressMs = std::min(MinuteMs - 1, Milliseconds(Field(profile, "exp_progress_seconds")));
  state.queueUpgrades = static_cast<int>(std::min<int64_t>(QueueUpgradeCosts.size(),
      std::max<int64_t>(0, Number(Field(tasks, "queue_capacity"), BaseQueueCapacity) - BaseQueueCapacity)));
  EnforceLimits(state);
  for (const auto& item : Objects(Field(tasks, "list"))) {
    const auto* task = FindTask(static_cast<int>(Number(Field(item, "id"))));
    if (task && !task->repeatable && Number(Field(item, "status")) == 3 && !Archived(state, task->id)) state.archived.push_back(task->id);
  }
  const auto& current = Field(tasks, "current");
  if (const auto* task = FindTask(static_cast<int>(Number(Field(current, "id"))))) {
    const auto duration = std::max<int64_t>(1000, Milliseconds(Field(current, "cost")));
    const auto started = Number(Field(current, "start_time"), 0, INT64_MAX / 1000) * 1000;
    // The snapshot does not say whether the task came from the queue.
    state.current = RunningTask{task->id, duration, std::min(duration, Milliseconds(Field(current, "elapsed_seconds"))), false,
        started ? started : nowMs};
  }
  for (const auto& entry : Objects(Field(tasks, "queue")))
    Enqueue(state, static_cast<int>(Number(Field(entry, "id"))), Number(Field(entry, "entry_id"), 0, INT64_MAX / 2));
  for (const auto& record : Objects(Field(tasks, "history"))) if (state.history.size() < 10) state.history.push_back(record);
  Observe(state, nowMs);
  return state;
}

Json Save(const State& state) {
  Json queue = Json::array();
  for (const auto& entry : state.queue) queue.push_back({{"entry_id", entry.entryId}, {"task_id", entry.taskId}});
  Json current = nullptr;
  if (state.current) current = {{"id", state.current->id}, {"duration_ms", state.current->durationMs},
      {"elapsed_ms", state.current->elapsedMs}, {"queued", state.current->queued}, {"started_at", state.current->startedAt}};
  return {{"version", 1}, {"attributes", state.attributes}, {"stars", state.stars},
      {"clothes", {{"current", state.clothes}, {"unlock", state.unlockedClothes}}}, {"failcount", state.failcount},
      {"achievements", state.achievements}, {"archived", state.archived}, {"current", current}, {"queue", queue},
      {"next_entry", state.nextEntry}, {"queue_upgrades", state.queueUpgrades}, {"history", state.history},
      {"exp_progress_ms", state.expProgressMs}};
}

State Load(const Json& saved) {
  if (!saved.is_object() || Field(saved, "version") != 1) throw std::runtime_error("Unsupported game save");
  auto state = Blank();
  ReadAttributes(state, Field(saved, "attributes"));
  ReadClothes(state, Field(saved, "clothes"));
  state.stars = Number(Field(saved, "stars"));
  state.failcount = Number(Field(saved, "failcount"));
  ReadAchievements(state, Field(saved, "achievements"));
  state.expProgressMs = Number(Field(saved, "exp_progress_ms"), 0, MinuteMs - 1);
  state.queueUpgrades = static_cast<int>(Number(Field(saved, "queue_upgrades"), 0, QueueUpgradeCosts.size()));
  state.nextEntry = std::max<int64_t>(1, Number(Field(saved, "next_entry"), 1, INT64_MAX / 2));
  EnforceLimits(state);
  const auto& archived = Field(saved, "archived");
  if (archived.is_array()) for (const auto& id : archived) {
    const auto* task = FindTask(static_cast<int>(Number(id)));
    if (task && !task->repeatable && !Archived(state, task->id)) state.archived.push_back(task->id);
  }
  const auto& current = Field(saved, "current");
  if (const auto* task = FindTask(static_cast<int>(Number(Field(current, "id"))))) {
    const auto duration = std::max<int64_t>(1000, Number(Field(current, "duration_ms"), 0, INT64_MAX / 2));
    state.current = RunningTask{task->id, duration, std::min(duration, Number(Field(current, "elapsed_ms"), 0, INT64_MAX / 2)),
        Field(current, "queued") == true, Number(Field(current, "started_at"), 0, INT64_MAX / 2)};
  }
  for (const auto& entry : Objects(Field(saved, "queue")))
    Enqueue(state, static_cast<int>(Number(Field(entry, "task_id"))), Number(Field(entry, "entry_id"), 0, INT64_MAX / 2));
  for (const auto& record : Objects(Field(saved, "history"))) if (state.history.size() < 10) state.history.push_back(record);
  return state;
}

void UpdateBuffs(State& state, const std::vector<std::string>& buffs, int64_t medal) {
  state.buffs.clear();
  for (const auto* buff : {"live", "dynamic", "guard", "monday", "birthday"})
    if (std::find(buffs.begin(), buffs.end(), buff) != buffs.end()) state.buffs.push_back(buff);
  state.medal = std::min<int64_t>(1000, std::max<int64_t>(0, medal));
}

void Advance(State& state, int64_t fromMs, int64_t toMs, const std::function<double()>& random) {
  int64_t cursor = fromMs;
  if (cursor < toMs) StartNext(state, cursor);
  while (cursor < toMs) {
    auto* task = state.current ? &*state.current : nullptr;
    int64_t delta = std::min(toMs - cursor, MinuteMs - state.expProgressMs);
    if (task) delta = std::min(delta, task->durationMs - task->elapsedMs);
    delta = std::max<int64_t>(0, delta);
    state.expProgressMs += delta;
    if (task) task->elapsedMs += delta;
    cursor += delta;
    if (state.expProgressMs >= MinuteMs) {
      state.expProgressMs -= MinuteMs;
      AddAttribute(state, "exp", ExpDiff(state));
      Achievements::Minute(state.achievements, DateUtc8(cursor));
      Observe(state, cursor);
    }
    if (task && task->elapsedMs >= task->durationMs) {
      const auto finished = *task;
      // Duration is fixed at start; success uses the attributes at completion.
      if (const auto* definition = FindTask(finished.id)) {
        const auto roll = static_cast<int64_t>(std::floor(random() * 400));
        Settle(state, *definition, roll >= std::llround((1 - SuccessRate(state, *definition)) * 400), finished.queued, cursor);
      }
      state.current.reset();
      StartNext(state, cursor);
    }
  }
}

void Execute(State& state, const Json& action, int64_t nowMs) {
  const auto& typeField = Field(action, "type");
  const auto type = typeField.is_string() ? typeField.get<std::string>() : std::string{};
  const auto id = Integer(action, "id");
  const auto* task = id && *id > 0 && *id <= INT32_MAX ? FindTask(static_cast<int>(*id)) : nullptr;
  if (type == "star") {
    if (!StarAvailable(state)) throw Error("五项属性均需达到 53 才能升星");
    Observe(state, nowMs);
    for (const auto* key : Attributes) AddAttribute(state, key, -53);
    ++state.stars;
  } else if (type == "attr.buy" || type == "attr.refund") {
    const auto& attrField = Field(action, "attr");
    const auto attr = attrField.is_string() ? attrField.get<std::string>() : std::string{};
    if (!IsAttribute(attr)) throw Error("无效属性");
    if (type == "attr.buy") {
      const auto cost = BuyCost(state);
      if (Attribute(state, attr) >= AttributeLimit(state)) throw Error("属性已达上限");
      if (Attribute(state, "exp") < cost) throw Error("经验不足");
      AddAttribute(state, attr, 1);
      AddAttribute(state, "exp", -cost);
      AddAttribute(state, "buycnt", 1);
    } else {
      if (Attribute(state, attr) <= 0 || Attribute(state, "buycnt") <= 0) throw Error("没有可返还的属性点");
      AddAttribute(state, attr, -1);
      AddAttribute(state, "exp", RefundGain(state));
      AddAttribute(state, "buycnt", -1);
    }
  } else if (type == "clothes") {
    if (!id || *id < 0 || *id >= static_cast<int64_t>(state.unlockedClothes.size()) || !state.unlockedClothes[*id])
      throw Error("衣装尚未解锁");
    state.clothes = static_cast<int>(*id);
  } else if (type == "queue.upgrade") {
    const auto cost = QueueUpgradeCost(state);
    if (!cost) throw Error("任务队列容量已全部解锁");
    if (state.stars < *cost) throw Error("星星不足，解锁下一个队列位置需要 " + std::to_string(*cost) + " 颗星星");
    Observe(state, nowMs);
    state.stars -= *cost;
    ++state.queueUpgrades;
    // Spending stars lowers attribute limits; overflow becomes experience.
    EnforceLimits(state);
  } else if (type == "task.start" || type == "task.queue") {
    if (!task) throw Error("任务不存在");
    if (Archived(state, task->id)) throw Error("该任务已经完成");
    if (!task->repeatable && Scheduled(state, task->id)) throw Error("该任务正在执行或排队");
    if (type == "task.start") {
      if (state.current || !state.queue.empty()) throw Error("已有任务在运行或排队，请加入队列");
      if (SuccessRate(state, *task) == 0) throw Error("任务成功率为 0，请先提升属性");
      state.current = RunningTask{task->id, TaskCost(state, *task) * 1000, 0, false, nowMs};
    } else {
      if (static_cast<int>(state.queue.size()) >= QueueCapacity(state))
        throw Error(QueueUpgradeCost(state) ? "任务队列已满，可消耗星星解锁更多位置" : "任务队列已满");
      Enqueue(state, task->id);
    }
  } else if (type == "task.cancel") {
    if (!state.current || !id || state.current->id != *id) throw Error("该任务当前未在运行");
    state.current.reset();
  } else if (type == "queue.remove" || type == "queue.move") {
    const auto entry = Integer(action, "entry_id");
    const auto it = std::find_if(state.queue.begin(), state.queue.end(), [&](const QueueEntry& e) { return entry && e.entryId == *entry; });
    if (it == state.queue.end()) throw Error("该排队任务已开始或已被移除");
    if (type == "queue.remove") {
      state.queue.erase(it);
    } else {
      const auto direction = Integer(action, "direction");
      if (!direction || (*direction != -1 && *direction != 1)) throw Error("无效的移动方向");
      const auto index = it - state.queue.begin();
      const auto target = index + *direction;
      if (target < 0 || target >= static_cast<int64_t>(state.queue.size())) throw Error("已到达队列边界");
      std::swap(state.queue[index], state.queue[target]);
    }
  } else {
    throw Error("未知操作");
  }
  Observe(state, nowMs);
  StartNext(state, nowMs);
}

void RecordTouches(State& state, int64_t count, int64_t nowMs) {
  Achievements::Increment(state.achievements, "touches", std::min<int64_t>(1000, std::max<int64_t>(0, count)));
  Observe(state, nowMs);
}

Json Snapshot(const State& state) {
  auto buffs = Json(state.buffs);
  if (state.failcount >= 2) buffs.push_back("fail");
  const auto upgradeCost = QueueUpgradeCost(state);
  Json current = nullptr, list = Json::array(), queue = Json::array();
  if (state.current) {
    const auto& running = *state.current;
    current = DescribeTask(state, *FindTask(running.id));
    current["status"] = 1;
    current["cost"] = running.durationMs / 1000.0;
    current["elapsed_seconds"] = running.elapsedMs / 1000.0;
    current["remaining_seconds"] = (running.durationMs - running.elapsedMs) / 1000.0;
    current["start_time"] = running.startedAt / 1000;
  }
  for (const auto& task : Tasks()) if (!state.current || state.current->id != task.id) list.push_back(DescribeTask(state, task));
  for (const auto& entry : state.queue) {
    auto item = DescribeTask(state, *FindTask(entry.taskId));
    item["entry_id"] = entry.entryId;
    queue.push_back(std::move(item));
  }
  const bool blocked = !state.current && !state.queue.empty() && SuccessRate(state, *FindTask(state.queue.front().taskId)) == 0;
  return {
      {"profile", {{"attributes", state.attributes}, {"starcnt", state.stars},
          {"clothes", {{"current", state.clothes}, {"unlock", state.unlockedClothes}}}, {"expdiff", ExpDiff(state)},
          {"buffs", buffs}, {"exp_progress_seconds", state.expProgressMs / 1000.0}, {"buycost", BuyCost(state)},
          {"revertgain", RefundGain(state)}, {"attr_limit", AttributeLimit(state)}, {"star_available", StarAvailable(state)}}},
      {"tasks", {{"current", current}, {"list", list}, {"queue", queue}, {"queue_capacity", QueueCapacity(state)},
          {"history", state.history}, {"queue_blocked", blocked},
          {"queue_upgrade", {{"cost", upgradeCost ? Json(*upgradeCost) : Json(nullptr)}, {"stars", state.stars},
              {"available", upgradeCost && state.stars >= *upgradeCost}}}}},
      {"achievements", Achievements::Describe(state.achievements)},
      {"save", {{"failcount", state.failcount}, {"achievements", state.achievements}}}};
}
}  // namespace GameRules
