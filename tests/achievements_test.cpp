#include "Achievements.hpp"
#include <cassert>
#include <iostream>
#include <set>

int main() {
  using namespace Achievements;
  constexpr int64_t now = 1800000000;
  assert(Catalog().size() == 50);
  std::set<std::string> ids;
  std::set<std::pair<std::string, int>> conditions;
  for (const auto& definition : Catalog()) {
    assert(ids.insert(definition.id).second);
    assert(conditions.insert({definition.metric, definition.target}).second);
    auto state = EmptyState();
    Peak(state, definition.metric, definition.target - 1);
    Evaluate(state, now);
    assert(!state["unlocked"].contains(definition.id));
    Peak(state, definition.metric, definition.target);
    auto earned = Evaluate(state, now);
    assert(!earned.empty());
    assert(state["unlocked"][definition.id] == now);
    assert(Evaluate(state, now + 1).empty());
    state = Parse(state.dump());
    assert(state["unlocked"][definition.id] == now); // Full-width timestamp survives.
    state["metrics"][definition.metric] = 0;
    assert(Evaluate(state, now + 2).empty());
    const auto described = Describe(state);
    for (const auto& item : described["list"])
      if (item["id"] == definition.id) assert(item["unlocked"] == true && item["progress"] == definition.target);
  }

  auto state = EmptyState();
  for (int i = 0; i < 60; ++i) Minute(state, "2026-10-03");
  assert(state["metrics"]["days"] == 1 && state["metrics"]["minutes"] == 60);
  for (int day = 1; day <= 30; ++day) Minute(state, "2026-11-" + std::string(day < 10 ? "0" : "") + std::to_string(day));
  assert(state["metrics"]["days"] == 30 && state["dates"].size() == 30);
  state = Parse(state.dump());
  Minute(state, "2026-11-30");
  assert(state["metrics"]["days"] == 30);

  state = EmptyState();
  for (int id = 1; id <= 13; ++id) Task(state, id, true, id <= 5);
  Observe(state, {});
  assert(state["metrics"]["successes"] == 13 && state["metrics"]["variety"] == 13);
  assert(state["metrics"]["queued_successes"] == 5);
  assert(state["metrics"]["best_streak"] == 13);
  Task(state, 1, false, false);
  assert(state["streak"] == 0);
  Task(state, 1, true, false);
  assert(state["metrics"]["recoveries"] == 1);
  assert(state["streak"] == 1 && state["metrics"]["best_streak"] == 13);
  auto before = state;
  Task(state, 999, true, true);
  assert(state == before);
  state = Parse("{\"metrics\":{\"minutes\":\"bad\"},\"unlocked\":[],\"dates\":[1,\"2026-10-03\",\"2026-10-03\"],\"streak\":null}");
  assert(Number(state["metrics"], "minutes") == 0 && state["dates"].size() == 1);
  assert(Parse("invalid JSON") == EmptyState());
  state = EmptyState();
  for (const auto& definition : Catalog()) Peak(state, definition.metric, definition.target);
  assert(Evaluate(state, now).size() == 50);
  assert(Describe(state)["unlocked"] == 50);
  std::cout << "PASS all 50 achievement boundaries, persistence, dates, streaks and malformed saves\n";
}
