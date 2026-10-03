#pragma once

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <map>
#include <limits>
#include <string>
#include <vector>

// Stable IDs and metrics are part of the save format. Keep them when changing
// display text or adding achievements in future releases.
namespace Achievements {
using Json = nlohmann::json;
struct Definition {
  const char* id;
  const char* title;
  const char* description;
  const char* category;
  const char* icon;
  const char* metric;
  int target;
};

inline const std::vector<Definition>& Catalog() {
  static const std::vector<Definition> definitions = {
    {"hello", "初次相伴", "累计陪伴 1 分钟", "陪伴", "hello", "minutes", 1},
    {"hour", "一小时的约定", "累计陪伴 60 分钟", "陪伴", "hour", "minutes", 60},
    {"ten_hours", "桌边老朋友", "累计陪伴 600 分钟", "陪伴", "ten_hours", "minutes", 600},
    {"fifty_hours", "长久的陪伴", "累计陪伴 3000 分钟", "陪伴", "fifty_hours", "minutes", 3000},
    {"days_3", "常来看看", "在 3 个不同的自然日陪伴至少 1 分钟", "陪伴", "days_3", "days", 3},
    {"days_7", "一周的回忆", "在 7 个不同的自然日陪伴至少 1 分钟", "陪伴", "days_7", "days", 7},
    {"days_30", "月光下的约定", "在 30 个不同的自然日陪伴至少 1 分钟", "陪伴", "days_30", "days", 30},
    {"touch_1", "打个招呼", "点击轴伊的身体部位 1 次", "互动", "touch_1", "touches", 1},
    {"touch_50", "熟悉的温度", "累计点击轴伊的身体部位 50 次", "互动", "touch_50", "touches", 50},
    {"touch_200", "默契满分", "累计点击轴伊的身体部位 200 次", "互动", "touch_200", "touches", 200},
    {"speed_10", "轻快步伐", "速度达到 10", "成长", "speed_10", "speed", 10},
    {"speed_50", "追风少女", "速度达到 50", "成长", "speed_50", "speed", 50},
    {"endurance_10", "再坚持一下", "耐力达到 10", "成长", "endurance_10", "endurance", 10},
    {"endurance_50", "永不疲倦", "耐力达到 50", "成长", "endurance_50", "endurance", 50},
    {"strength_10", "小小力量", "力量达到 10", "成长", "strength_10", "strength", 10},
    {"strength_50", "力能扛鼎", "力量达到 50", "成长", "strength_50", "strength", 50},
    {"will_10", "坚定的心", "毅力达到 10", "成长", "will_10", "will", 10},
    {"will_50", "不灭的决心", "毅力达到 50", "成长", "will_50", "will", 50},
    {"intellect_10", "灵光一现", "智力达到 10", "成长", "intellect_10", "intellect", 10},
    {"intellect_50", "聪明的轴伊", "智力达到 50", "成长", "intellect_50", "intellect", 50},
    {"balanced_10", "均衡发展", "五项属性同时达到 10", "成长", "balanced_10", "balanced", 10},
    {"balanced_50", "五角星战士", "五项属性同时达到 50", "成长", "balanced_50", "balanced", 50},
    {"balanced_100", "完美五边形", "五项属性同时达到 100", "成长", "balanced_100", "balanced", 100},
    {"exp_10000", "经验储蓄罐", "持有经验达到 10000", "成长", "exp_10000", "exp", 10000},
    {"exp_100000", "经验小富翁", "持有经验达到 100000", "成长", "exp_100000", "exp", 100000},
    {"success_1", "旗开得胜", "成功完成 1 次任务", "任务", "success_1", "successes", 1},
    {"success_10", "渐入佳境", "累计成功完成 10 次任务", "任务", "success_10", "successes", 10},
    {"success_50", "任务达人", "累计成功完成 50 次任务", "任务", "success_50", "successes", 50},
    {"queued_5", "井井有条", "成功完成 5 次从队列自动开始的任务", "任务", "queued_5", "queued_successes", 5},
    {"failure_1", "成长的学费", "经历 1 次任务失败", "任务", "failure_1", "failures", 1},
    {"recovery", "重新站起来", "任务失败后，下一次结算的任务成功", "任务", "recovery", "recoveries", 1},
    {"streak_5", "势如破竹", "连续成功完成 5 次任务（失败会中断连胜）", "任务", "streak_5", "best_streak", 5},
    {"variety_7", "多面手", "成功完成 7 种不同的任务", "任务", "variety_7", "variety", 7},
    {"task_1", "瓶盖克星", "成功完成「拧瓶盖」", "任务", "task_1", "task.1", 1},
    {"task_2", "起跑线", "成功完成「跑步 800m」", "任务", "task_2", "task.2", 1},
    {"task_3", "哈喽哈喽", "成功完成「日常直播」", "任务", "task_3", "task.3", 1},
    {"task_4", "健身环重启", "成功完成「健身环直播」", "任务", "task_4", "task.4", 1},
    {"task_5", "夜行电台", "成功完成「困困夜行电台直播」", "任务", "task_5", "task.5", 1},
    {"task_6", "采集卡没有延迟", "成功完成「游戏直播」", "任务", "task_6", "task.6", 1},
    {"task_7", "歌声与回忆", "成功完成「歌回直播」", "任务", "task_7", "task.7", 1},
    {"task_9", "舞步飞扬", "成功完成「舞蹈课」", "任务", "task_9", "task.9", 1},
    {"task_10", "世界那么大", "成功完成「旅游」", "任务", "task_10", "task.10", 1},
    {"task_11", "联动策划人", "成功完成「策划联动直播」", "任务", "task_11", "task.11", 1},
    {"task_13", "全能之主", "成功完成「全能之主」", "任务", "task_13", "task.13", 1},
    {"dress", "盛装登场", "解锁礼服衣装", "衣装", "dress", "dress", 1},
    {"winter", "冬日暖意", "解锁冬装衣装", "衣装", "winter", "winter", 1},
    {"wardrobe", "衣橱收藏家", "集齐默认、礼服和冬装三套衣装", "衣装", "wardrobe", "clothes", 3},
    {"star_1", "第一颗星", "完成 1 次升星", "升星", "star_1", "stars", 1},
    {"star_3", "星光闪耀", "达到 3 星", "升星", "star_3", "stars", 3},
    {"star_5", "璀璨星河", "达到 5 星", "升星", "star_5", "stars", 5},
  };
  return definitions;
}

inline Json EmptyState() {
  return {{"version", 1}, {"metrics", Json::object()}, {"unlocked", Json::object()},
          {"dates", Json::array()}, {"streak", 0}, {"last_failed", false}};
}

inline int64_t Number(const Json& object, const std::string& key) {
  auto it = object.find(key);
  if (it == object.end() || !it->is_number_integer()) return 0;
  if (it->is_number_unsigned()) return static_cast<int64_t>(std::min<uint64_t>(
      it->get<uint64_t>(), std::numeric_limits<int64_t>::max()));
  return std::max<int64_t>(0, it->get<int64_t>());
}

inline Json Parse(const std::string& saved) {
  auto result = EmptyState();
  auto source = Json::parse(saved, nullptr, false);
  if (!source.is_object()) return result;
  for (auto field : {"metrics", "unlocked"}) {
    if (source.contains(field) && source[field].is_object()) {
      for (const auto& [key, value] : source[field].items()) {
        if (value.is_number_integer()) result[field][key] = Number(source[field], key);
      }
    }
  }
  if (source.contains("dates") && source["dates"].is_array()) {
    for (const auto& date : source["dates"]) {
      if (date.is_string() && date.get<std::string>().size() == 10 &&
          std::find(result["dates"].begin(), result["dates"].end(), date) == result["dates"].end())
        result["dates"].push_back(date);
      if (result["dates"].size() == 30) break;
    }
  }
  result["streak"] = Number(source, "streak");
  result["last_failed"] = source.contains("last_failed") && source["last_failed"] == true;
  return result;
}

inline void Peak(Json& state, const std::string& metric, int64_t value) {
  state["metrics"][metric] = std::max(Number(state["metrics"], metric), value);
}
inline void Increment(Json& state, const std::string& metric) {
  state["metrics"][metric] = std::min<int64_t>(999999999, Number(state["metrics"], metric) + 1);
}
inline void Observe(Json& state, const std::map<std::string, int>& values) {
  for (const auto& [key, value] : values) Peak(state, key, std::max(0, value));
  int64_t variety = 0;
  for (int id = 1; id <= 13; ++id)
    variety += Number(state["metrics"], "task." + std::to_string(id)) > 0;
  Peak(state, "variety", variety);
}
inline void Minute(Json& state, const std::string& date) {
  Increment(state, "minutes");
  auto& dates = state["dates"];
  if (dates.size() < 30 && std::find(dates.begin(), dates.end(), date) == dates.end()) dates.push_back(date);
  Peak(state, "days", dates.size());
}
inline void Task(Json& state, int id, bool success, bool queued) {
  if (id < 1 || id > 13) return; // Developer-only tasks don't award achievements.
  if (success) {
    Increment(state, "successes");
    Increment(state, "task." + std::to_string(id));
    if (queued) Increment(state, "queued_successes");
    if (state["last_failed"] == true) Increment(state, "recoveries");
    state["streak"] = Number(state, "streak") + 1;
    Peak(state, "best_streak", Number(state, "streak"));
  } else {
    Increment(state, "failures");
    state["streak"] = 0;
  }
  state["last_failed"] = !success;
}
inline Json Evaluate(Json& state, int64_t now) {
  auto unlocked = Json::array();
  for (const auto& definition : Catalog()) {
    if (Number(state["metrics"], definition.metric) < definition.target ||
        Number(state["unlocked"], definition.id) > 0) continue;
    state["unlocked"][definition.id] = now;
    unlocked.push_back({{"id", definition.id}, {"title", definition.title}, {"icon", definition.icon}});
  }
  return unlocked;
}
inline Json Describe(const Json& state) {
  auto list = Json::array();
  int count = 0;
  for (const auto& definition : Catalog()) {
    auto time = Number(state["unlocked"], definition.id);
    count += time > 0;
    list.push_back({{"id", definition.id}, {"title", definition.title},
        {"description", definition.description}, {"category", definition.category},
        {"icon", definition.icon}, {"target", definition.target},
        {"progress", time > 0 ? definition.target : std::min<int64_t>(definition.target, Number(state["metrics"], definition.metric))},
        {"unlocked", time > 0}, {"unlocked_at", time}});
  }
  return {{"total", Catalog().size()}, {"unlocked", count}, {"list", list}};
}
}
