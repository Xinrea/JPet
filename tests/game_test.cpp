// Rules regression tests, ported from the former Cloudflare game service.
#include "GameRules.hpp"

#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>

using namespace GameRules;

namespace {
constexpr int64_t Epoch = 1'800'000'000'000;
int failures = 0;
void Check(bool condition, const char* name) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL " << name << "\n";
  }
}
template <typename F>
void Rejects(F&& action, const std::string& fragment, const char* name) {
  try {
    action();
    Check(false, name);
  } catch (const Error& error) {
    Check(std::string(error.what()).find(fragment) != std::string::npos, name);
  }
}

struct Game {
  State state = Create(Json::object(), Epoch);
  int64_t clock = Epoch;
  // Runs the game for `ms`; rolls of 0.99 succeed unless the success rate is 0.
  void Run(int64_t ms, double roll = 0.99) {
    Advance(state, clock, clock + ms, [roll] { return roll; });
    clock += ms;
  }
  void Do(const Json& action) { Execute(state, action, clock); }
};
Game Trained(int64_t value = 50) {
  Game game;
  for (const auto* key : Attributes) game.state.attributes[key] = value;
  return game;
}
const TaskDefinition& Task(int id) { return *FindTask(id); }
}  // namespace

int main() {
  {
    Check(Tasks().size() == 13 && Tasks().front().id == 1 && Tasks().back().id == 13, "keeps the 13 tasks");
    Check(Snapshot(Game().state)["achievements"]["total"] == 50, "keeps the 50 achievements");
    const auto state = Game().state;
    Check(state.attributes.at("speed") == 2 && state.attributes.at("intellect") == 4 && state.attributes.at("exp") == 0,
          "new games start with the original attributes");
  }
  {
    const auto state = Create({{"tasks", {{{"id", 1}, {"status", 1}, {"cost_snapshot", 0}, {"elapsed_seconds", 20}}}}}, Epoch);
    Check(state.current && state.current->durationMs == 300'000 && state.current->elapsedMs == 20'000,
          "restores a legacy running task without a saved duration");
  }
  {
    Game game;
    game.Run(30'000);
    Check(game.state.expProgressMs == 30'000 && game.state.attributes["exp"] == 0, "keeps sub-minute progress");
    game.Run(30'000);
    Check(game.state.expProgressMs == 0 && game.state.attributes["exp"] == 1, "grants experience every minute");
    Check(game.state.achievements["metrics"]["minutes"] == 1, "counts companion minutes");
  }
  {
    auto game = Trained();
    game.Do({{"type", "task.start"}, {"id", 1}});
    const auto duration = game.state.current->durationMs;
    game.Run(30'000);
    Check(game.state.current->elapsedMs == 30'000 && game.state.history.empty(), "tasks progress with running time");
    game.Run(duration - 30'000);
    Check(!game.state.current && game.state.history.size() == 1, "tasks settle when their duration completes");
    const auto exp = game.state.attributes["exp"];
    game.Run(0);
    Check(game.state.attributes["exp"] == exp, "rewards are granted once");
  }
  {
    Game game;
    Check(ExpDiff(game.state) == 1, "base experience rate");
    UpdateBuffs(game.state, {"live", "guard", "invalid"}, 30);
    Check(ExpDiff(game.state) == static_cast<int64_t>(std::floor((1 + std::ceil(499 * (1 - 0.9 * 0.9))) * 2 * 1.25)),
          "medal and buff modifiers");
    Check(game.state.buffs == std::vector<std::string>({"live", "guard"}), "unknown buffs are ignored");
  }
  {
    Game game;
    game.Run(60'000);
    Check(game.state.attributes["exp"] == 1, "experience before a buff");
    UpdateBuffs(game.state, {"live"}, 0);
    game.Run(60'000);
    Check(game.state.attributes["exp"] == 3, "a changed buff applies after settling the previous interval");
  }
  {
    auto game = Trained();
    Check(TaskCost(game.state, Task(6)) == 3260, "speed shortens tasks");
    Check(SuccessRate(game.state, Task(6)) == 0.825, "success rate formula");
    game.state.stars = 14;
    Check(SuccessRate(game.state, Task(6)) == 0, "stars lower success rate");
  }
  {
    Game game;
    game.state.attributes["exp"] = 100;
    game.Do({{"type", "attr.buy"}, {"attr", "speed"}});
    Check(game.state.attributes["speed"] == 3 && game.state.attributes["exp"] == 90 && game.state.attributes["buycnt"] == 1, "buys an attribute");
    game.Do({{"type", "attr.refund"}, {"attr", "speed"}});
    Check(game.state.attributes["speed"] == 2 && game.state.attributes["exp"] == 95 && game.state.attributes["buycnt"] == 0, "refunds half");
    Rejects([&] { game.Do({{"type", "attr.buy"}, {"attr", "exp"}}); }, "无效属性", "rejects invalid attributes");
    Rejects([&] { game.Do({{"type", "attr.refund"}, {"attr", "speed"}}); }, "没有可返还", "rejects refunds without purchases");
  }
  {
    Game game;
    const auto before = game.state.attributes;
    Rejects([&] { game.Do({{"type", "attr.buy"}, {"attr", "intellect"}}); }, "经验不足", "rejects unaffordable purchases");
    Check(game.state.attributes == before, "rejected purchases change nothing");
  }
  {
    auto game = Trained(53);
    game.Do({{"type", "star"}});
    Check(game.state.stars == 1 && game.state.attributes["speed"] == 0, "stars cost 53 of each attribute");
    Check(game.state.achievements["unlocked"]["balanced_50"] == Epoch / 1000, "achievements are earned before spending");
    Check(Snapshot(game.state)["tasks"]["queue_capacity"] == 2, "stars do not add queue capacity");
  }
  {
    Game game;
    game.state.stars = 18;
    for (size_t level = 0; level < QueueUpgradeCosts.size(); ++level) {
      const auto upgrade = Snapshot(game.state)["tasks"]["queue_upgrade"];
      Check(upgrade["cost"] == QueueUpgradeCosts[level] && upgrade["available"] == true, "offers the next upgrade");
      const auto balance = game.state.stars;
      game.Do({{"type", "queue.upgrade"}});
      Check(game.state.stars == balance - QueueUpgradeCosts[level] && game.state.queueUpgrades == static_cast<int>(level + 1) &&
            Snapshot(game.state)["tasks"]["queue_capacity"] == level + 3, "upgrades spend stars for one slot");
    }
    Check(game.state.stars == 0 && Snapshot(game.state)["tasks"]["queue_upgrade"] == Json({{"cost", nullptr}, {"stars", 0}, {"available", false}}),
          "all slots unlocked");
    game.state.stars = 100;
    Rejects([&] { game.Do({{"type", "queue.upgrade"}}); }, "全部解锁", "rejects upgrades past six slots");
    Check(game.state.stars == 100 && QueueCapacity(game.state) == 6, "capacity is capped at six");
  }
  for (int level = 0; level < 4; ++level) {
    Game game;
    game.state.queueUpgrades = level;
    game.state.stars = QueueUpgradeCosts[level] - 1;
    Check(Snapshot(game.state)["tasks"]["queue_upgrade"]["available"] == false, "unaffordable upgrade is unavailable");
    const auto before = Save(game.state);
    Rejects([&] { game.Do({{"type", "queue.upgrade"}}); }, "星星不足", "rejects unaffordable upgrades");
    Check(Save(game.state) == before, "rejected upgrades change nothing");
    ++game.state.stars;
    game.Do({{"type", "queue.upgrade"}});
    Check(game.state.stars == 0 && game.state.queueUpgrades == level + 1, "affordable upgrade succeeds");
  }
  {
    auto game = Trained();
    game.state.stars = 1;
    game.Do({{"type", "queue.upgrade"}});
    for (const auto* key : Attributes) game.state.attributes[key] = 53;
    game.Do({{"type", "star"}});
    Check(game.state.stars == 1 && QueueCapacity(game.state) == 3, "purchased capacity survives earning stars");
  }
  {
    auto game = Trained(130);
    game.state.stars = 3;
    game.Do({{"type", "queue.upgrade"}});
    Check(game.state.stars == 2 && game.state.achievements["metrics"]["stars"] == 3 &&
          game.state.achievements["unlocked"]["star_3"] == Epoch / 1000, "star achievements are kept");
    Check(game.state.attributes["speed"] == 120 && game.state.attributes["intellect"] == 120 &&
          game.state.attributes["exp"] == 5 * 10 * 26500, "attributes above lower limits become experience");
  }
  {
    auto state = Create({{"profile", {{"starcnt", 5}}}, {"queue", {{{"task_id", 1}}, {{"task_id", 2}}, {{"task_id", 4}}, {{"task_id", 6}}}}}, Epoch);
    Check(state.queue.size() == 4 && state.queue[3].taskId == 6 && state.stars == 5 && state.queueUpgrades == 0,
          "legacy queues above the new capacity are kept without free upgrades");
    Rejects([&] { Execute(state, {{"type", "task.queue"}, {"id", 1}}, Epoch); }, "队列已满", "full legacy queues accept no new tasks");
  }
  {
    auto game = Trained();
    game.state.stars = 1;
    game.Do({{"type", "task.start"}, {"id", 2}});
    game.Do({{"type", "task.queue"}, {"id", 4}});
    game.Do({{"type", "task.queue"}, {"id", 6}});
    Rejects([&] { game.Do({{"type", "task.queue"}, {"id", 1}}); }, "消耗星星", "full queues offer upgrades");
    const auto running = game.state.current->id;
    game.Do({{"type", "queue.upgrade"}});
    game.Do({{"type", "task.queue"}, {"id", 1}});
    Check(game.state.current->id == running && game.state.queue.size() == 3 && game.state.queue[2].taskId == 1,
          "a purchased slot accepts another task without interrupting the running one");
  }
  {
    auto game = Trained();
    game.Do({{"type", "task.start"}, {"id", 2}});
    game.Do({{"type", "task.queue"}, {"id", 8}});
    Rejects([&] { game.Do({{"type", "task.queue"}, {"id", 8}}); }, "正在执行或排队", "one-off tasks are queued once");
    game.Do({{"type", "task.queue"}, {"id", 6}});
    Rejects([&] { game.Do({{"type", "task.queue"}, {"id", 4}}); }, "队列已满", "queue capacity");
    game.Do({{"type", "queue.move"}, {"entry_id", 2}, {"direction", -1}});
    game.Do({{"type", "task.cancel"}, {"id", 2}});
    Check(game.state.current->id == 6 && game.state.queue[0].taskId == 8, "reordered queue starts in order after cancel");
    Rejects([&] { game.Do({{"type", "queue.move"}, {"entry_id", 1}, {"direction", -1}}); }, "队列边界", "queue bounds");
  }
  {
    Game game;
    game.Do({{"type", "task.queue"}, {"id", 13}});
    Check(Snapshot(game.state)["tasks"]["queue_blocked"] == true && !game.state.current, "an impossible queue head blocks");
    for (const auto* key : Attributes) game.state.attributes[key] = 100;
    game.Run(1);
    Check(game.state.current && game.state.current->id == 13, "the queue resumes once conditions change");
  }
  {
    auto game = Trained(100);
    game.Do({{"type", "task.start"}, {"id", 2}});
    game.Run(game.state.current->durationMs);
    Check(game.state.history[0]["rewards"]["exp"] == 79500 && game.state.attributes["speed"] == 100 &&
          game.state.achievements["metrics"]["successes"] == 1, "reward overflow becomes experience");
  }
  {
    auto game = Trained();
    game.Do({{"type", "task.start"}, {"id", 2}});
    game.state.stars = 14;
    game.Run(game.state.current->durationMs);
    Check(!game.state.current && game.state.history[0]["success"] == false && game.state.failcount == 1,
          "guaranteed failures settle");
  }
  {
    auto game = Trained();
    game.Do({{"type", "task.start"}, {"id", 8}});
    game.Run(game.state.current->durationMs);
    Check(game.state.unlockedClothes[1], "tasks unlock clothes");
    game.Do({{"type", "clothes"}, {"id", 1}});
    Check(game.state.clothes == 1 && game.state.achievements["unlocked"].contains("dress"), "unlocked clothes can be worn");
    Rejects([&] { game.Do({{"type", "task.start"}, {"id", 8}}); }, "已经完成", "one-off tasks are archived");
    Rejects([&] { game.Do({{"type", "clothes"}, {"id", 2}}); }, "尚未解锁", "locked clothes are rejected");
  }
  {
    const auto state = Create({{"profile", {{"attributes", {{"speed", 10}, {"strength", 10}, {"endurance", 10}}}}},
        {"tasks", {{{"id", 2}, {"status", 2}, {"success", true}}, {{"id", 6}, {"status", 1}, {"cost_snapshot", 200}, {"elapsed_seconds", 25}}}},
        {"queue", {{{"task_id", 4}}}}}, Epoch);
    Check(state.attributes.at("speed") == 11 && state.current->elapsedMs == 25'000 && state.queue.size() == 1,
          "imports old progress and settles an unclaimed reward once");
  }
  {
    auto game = Trained();
    game.state.stars = 1;
    game.Do({{"type", "queue.upgrade"}});
    game.Do({{"type", "task.start"}, {"id", 2}});
    game.Do({{"type", "task.queue"}, {"id", 4}});
    const auto before = Save(game.state);
    for (const auto& action : {Json{{"type", "reset"}}, Json{{"type", "share"}, {"enabled", true}}, Json::object(), Json::array()})
      Rejects([&] { game.Do(action); }, "未知操作", "unknown actions are rejected");
    Check(Save(game.state) == before, "unknown actions change nothing");
  }
  {
    Game game;
    game.clock = 1'791'043'170'000;  // 2026-10-03 23:59:30 UTC+8
    game.Run(30'000);
    game.Run(60'000);
    Check(game.state.achievements["dates"] == Json({"2026-10-04"}), "companion days use UTC+8 dates");
    game.clock -= 120'000;
    game.Run(60'000);
    Check(game.state.achievements["dates"] == Json({"2026-10-04", "2026-10-03"}) && game.state.achievements["metrics"]["days"] == 2,
          "distinct days are counted");
  }
  {
    Game game;
    RecordTouches(game.state, 1, Epoch);
    Check(game.state.achievements["unlocked"].contains("touch_1"), "touches unlock achievements");
    RecordTouches(game.state, 5000, Epoch);
    Check(game.state.achievements["metrics"]["touches"] == 1001, "touch batches are bounded");
  }
  {
    auto game = Trained();
    game.state.stars = 2;
    game.Do({{"type", "queue.upgrade"}});
    game.Do({{"type", "task.start"}, {"id", 8}});
    game.Run(game.state.current->durationMs);
    game.Do({{"type", "task.queue"}, {"id", 2}});
    game.Do({{"type", "task.queue"}, {"id", 4}});
    game.Run(45'500);
    const auto saved = Save(game.state);
    Check(Save(Load(saved)) == saved, "saves round-trip");
    Check(Save(Load(Json::parse(saved.dump()))) == saved, "saves round-trip through text");
    for (const auto& invalid : {Json(), Json::array(), Json{{"version", 2}}})
      try { Load(invalid); Check(false, "unsupported saves are rejected"); } catch (const std::runtime_error&) {}
    auto corrupt = saved;
    corrupt["queue"].push_back({{"entry_id", 99}, {"task_id", 99}});
    corrupt["current"]["id"] = 99;
    corrupt["archived"].push_back(2);
    corrupt["exp_progress_ms"] = 1e12;
    const auto repaired = Load(corrupt);
    Check(!repaired.current && repaired.queue.size() == game.state.queue.size() && repaired.archived == game.state.archived &&
          repaired.expProgressMs < MinuteMs, "corrupt save entries are dropped");

    // The removed cloud service sent snapshots in the same shape as Snapshot().
    auto expected = game.state;
    expected.current->queued = false;
    const auto imported = FromCloudSnapshot(Snapshot(game.state), game.clock);
    Check(Save(imported) == Save(expected), "cloud snapshots import the whole game");
  }
  {
    const Json cloud = {{"schema", 1}, {"uid", "123"}, {"revision", 7}, {"online", false}, {"share", true},
        {"profile", {{"attributes", {{"speed", 30}, {"endurance", 31}, {"strength", 32}, {"will", 33}, {"intellect", 34}, {"exp", 4567}, {"buycnt", 6}}},
            {"starcnt", 1}, {"clothes", {{"current", 1}, {"unlock", {true, true, false}}}}, {"exp_progress_seconds", 12.5}}},
        {"tasks", {{"current", {{"id", 3}, {"cost", 3600}, {"elapsed_seconds", 100.25}, {"start_time", 1700000000}, {"paused", true}}},
            {"list", {{{"id", 8}, {"status", 3}}, {{"id", 2}, {"status", 0}}}},
            {"queue", {{{"entry_id", 41}, {"id", 4}}, {{"entry_id", 42}, {"id", 2}}}}, {"queue_capacity", 3},
            {"history", {{{"id", 8}, {"title", "新衣装发布回 - 礼服"}, {"success", true}}}}}},
        {"save", {{"failcount", 2}, {"achievements", {{"version", 1}, {"metrics", {{"minutes", 90}, {"task.8", 1}}},
            {"unlocked", {{"hello", 1700000000}}}, {"dates", {"2026-10-01"}}, {"streak", 1}, {"last_failed", false}}}}}};
    const auto state = FromCloudSnapshot(cloud, Epoch);
    Check(state.attributes.at("exp") == 4567 && state.attributes.at("buycnt") == 6 && state.stars == 1 &&
          state.clothes == 1 && state.unlockedClothes[1] && state.failcount == 2, "imports cloud profile");
    Check(state.current && state.current->id == 3 && state.current->durationMs == 3'600'000 && state.current->elapsedMs == 100'250 &&
          state.current->startedAt == 1'700'000'000'000, "imports the running cloud task");
    Check(state.queue.size() == 2 && state.queue[0].entryId == 41 && state.nextEntry == 43 && state.queueUpgrades == 1,
          "imports the cloud queue and its purchased slots");
    Check(state.archived == std::vector<int>{8} && state.expProgressMs == 12'500 && state.history.size() == 1, "imports archive and progress");
    Check(state.achievements["metrics"]["minutes"] == 90 && state.achievements["unlocked"]["hello"] == 1700000000 &&
          state.achievements["unlocked"].contains("dress"), "imports achievements and keeps their dates");
  }
  if (failures) return EXIT_FAILURE;
  std::cout << "PASS game rules: progression, tasks, queue, stars, clothes, saves and cloud import\n";
}
