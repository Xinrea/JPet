#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <time.h>
#include <nlohmann/json.hpp>

#include "LAppPal.hpp"
#include "LAppDefine.hpp"
#include "WinToastEventHandler.h"

using std::map;
using std::wstring;
using std::string;

enum class TStatus {
  IDLE, RUNNING, WAIT_SETTLE, ARCHIVED
};

struct SpecialReward {
  wstring title;
  wstring desc;
  string linked_key;

  SpecialReward() = default;

  SpecialReward(const std::wstring &title, const std::wstring &desc,
                const std::string &key)
      : title(title), desc(desc), linked_key(key) {}
};

class GameTask {
public:
  int id;
  time_t start_time;
  time_t end_time;
  int cost;
  int cost_snapshot;
  wstring title = L"";
  wstring desc = L"";
  bool success;
  bool repeatable;
  TStatus status;
  map<string, int> requirements;
  map<string, int> rewards;
  std::shared_ptr<SpecialReward> special;

  GameTask() = default;

  void Load();

  void Dump();

  void TryDone();

  void Notify(const wstring& title, const wstring& content,
                              WinToastEventHandler* handler);
  int GetCurrentCost();

  static inline const wstring ctasks = LR"(
  [
      {
        "id": 1,
        "cost": 300,
        "title": "拧瓶盖",
        "desc": "「我真的能办到吗...」轴伊呆呆地凝望着饮料瓶",
        "requirements": {
          "intellect": 9,
          "strength": 9
        },
        "rewards": {
          "exp": 30
        },
        "repeatable": true
      },
      {
        "id": 2,
        "cost": 3600,
        "title": "跑步 800m",
        "desc": "跑步训练！这是真是存在的吗？",
        "requirements": {
          "endurance": 5,
          "strength": 3
        },
        "rewards": {
          "endurance": 1,
          "strength": 1,
          "speed": 1
        },
        "repeatable": true
      },
      {
        "id": 3,
        "cost": 7200,
        "title": "日常直播",
        "desc": "「哈喽哈喽晚上好！」",
        "requirements": {
          "will": 5,
          "intellect": 3
        },
        "rewards": {
          "endurance": 1,
          "intellect": 1
        },
        "repeatable": true
      },
      {
        "id": 4,
        "cost": 7200,
        "title": "健身环直播",
        "desc": "*轴伊从尘封的抽屉中刨出了健身环",
        "requirements": {
          "endurance": 7,
          "strength": 7
        },
        "rewards": {
          "speed": 1,
          "endurance": 1,
          "strength": 1
        },
        "repeatable": true
      },
      {
        "id": 5,
        "cost": 7200,
        "title": "困困夜行电台直播",
        "desc": "闲聊与读故事 time",
        "requirements": {
          "endurance": 5,
          "will": 3
        },
        "rewards": {
          "will": 1,
          "intellect": 1
        },
        "repeatable": true
      },
      {
        "id": 6,
        "cost": 7200,
        "title": "游戏直播",
        "desc": "「是采集卡延迟，真的。」",
        "requirements": {
          "speed": 5,
          "will": 3
        },
        "rewards": {
          "speed": 1,
          "intellect": 1
        },
        "repeatable": true
      },
      {
        "id": 7,
        "cost": 7200,
        "title": "歌回直播",
        "desc": "「屋内的湿气 像储存爱你的记忆~」",
        "requirements": {
          "will": 3,
          "intellect": 5
        },
        "rewards": {
          "will": 2
        },
        "repeatable": true
      },
      {
        "id": 8,
        "cost": 7200,
        "title": "新衣装发布回 - 礼服",
        "desc": "新衣装发布啦~",
        "requirements": {
          "speed": 10,
          "endurance": 10,
          "strength": 10,
          "will": 10,
          "intellect": 10
        },
        "special": {
          "title": "礼服",
          "desc": "解锁新衣装 - 礼服",
          "linked_key": "clothes.1.active"
        },
        "repeatable": false
      },
      {
        "id": 9,
        "cost": 14400,
        "title": "舞蹈课",
        "desc": "*轴伊正在等车中",
        "requirements": {
          "endurance": 20,
          "strength": 15
        },
        "rewards": {
          "speed": 3,
          "endurance": 2
        },
        "repeatable": true
      },
      {
        "id": 10,
        "cost": 28800,
        "title": "旅游",
        "desc": "*轴伊正在收拾东西出门",
        "requirements": {
          "endurance": 15,
          "strength": 10
        },
        "rewards": {
          "endurance": 2,
          "intellect": 3
        },
        "repeatable": false
      },
      {
        "id": 11,
        "cost": 14400,
        "title": "策划联动直播",
        "desc": "「Work work」",
        "requirements": {
          "will": 25,
          "intellect": 25
        },
        "rewards": {
          "will": 2,
          "intellect": 3
        },
        "repeatable": true
      },
      {
        "id": 12,
        "cost": 7200,
        "title": "新衣装发布回 - 冬装",
        "desc": "冬装发布啦~",
        "requirements": {
          "speed": 50,
          "endurance": 50,
          "strength": 50,
          "will": 50,
          "intellect": 50
        },
        "special": {
          "title": "冬装",
          "desc": "解锁新衣装 - 冬装",
          "linked_key": "clothes.2.active"
        },
        "repeatable": false
      },
      {
        "id": 13,
        "cost": 86400,
        "title": "全能之主",
        "desc": "五角星战士轴伊在此",
        "requirements": {
          "speed": 100,
          "endurance": 100,
          "strength": 100,
          "will": 100,
          "intellect": 100
        },
        "rewards": {
          "exp": 500000
        },
        "repeatable": false
      }
  ]
)";

  static std::vector<std::shared_ptr<GameTask>> InitTasks() {
    std::vector<std::shared_ptr<GameTask>> tasks;
    auto presets = nlohmann::json::parse(ctasks);
    LAppPal::PrintLog(LogLevel::Debug, "Preset parsed");
    if (!presets.is_array()) {
      LAppPal::PrintLog(LogLevel::Debug, "Preset not array");
    }
    for (const auto& t : presets) {
      // create tasks
      std::shared_ptr<GameTask> task = std::make_shared<GameTask>();
      task->id = t["id"].get<int>();
      task->cost = t["cost"].get<int>();
      task->title = LAppPal::StringToWString(t["title"].get<string>());
      task->desc = LAppPal::StringToWString(t["desc"].get<string>());
      for (const auto& [key, value] : t["requirements"].items()) {
        task->requirements[key] = value.get<int>();
      }
      if (t.contains("rewards")) {
        for (const auto &[key, value] : t["rewards"].items()) {
          task->rewards[key] = value.get<int>();
        }
      }
      if (t.contains("special")) {
        task->special = std::make_shared<SpecialReward>();
        task->special->title = LAppPal::StringToWString(t["special"]["title"].get<string>());
        task->special->desc = LAppPal::StringToWString(t["special"]["desc"].get<string>());
        task->special->linked_key = t["special"]["linked_key"].get<string>();
      }
      task->repeatable = t["repeatable"].get<bool>();
      task->Load();
      tasks.push_back(task);
    }
    if (LAppDefine::DebugLogEnable) {
      std::shared_ptr<GameTask> taskDebug = std::make_shared<GameTask>();
      taskDebug->id = 999;
      taskDebug->cost = 10;
      taskDebug->title = L"开挂";
      taskDebug->desc = L"看在你是测试的原因就原谅你了";
      taskDebug->requirements["speed"] = 0;
      taskDebug->rewards["speed"] = 100;
      taskDebug->rewards["endurance"] = 100;
      taskDebug->rewards["strength"] = 100;
      taskDebug->rewards["will"] = 100;
      taskDebug->rewards["intellect"] = 100;
      taskDebug->rewards["exp"] = 500000;
      taskDebug->Load();
      tasks.push_back(taskDebug);
    }
    return tasks;
  }
};
