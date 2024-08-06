#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <time.h>

#include "LAppPal.hpp"
#include "wintoastlib.h"
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

  static std::vector<std::shared_ptr<GameTask>> InitTasks() {
    std::vector<std::shared_ptr<GameTask>> tasks;
    // create tasks
    std::shared_ptr<GameTask> task1 = std::make_shared<GameTask>();
    task1->id = 1;
    task1->cost = 10;
    task1->title = L"拧瓶盖";
    task1->desc = L"「我真的能办到吗...」";
    task1->requirements["strength"] = 1;
    task1->rewards["will"] = 10;
    task1->rewards["strength"] = 1;
    task1->repeatable = true;
    task1->Load();
    tasks.push_back(task1);

    std::shared_ptr<GameTask> task2 = std::make_shared<GameTask>();
    task2->id = 2;
    task2->cost = 120;
    task2->title = L"跑步 800m";
    task2->desc = L"简单跑个 800m";
    task2->requirements["strength"] = 2;
    task2->requirements["endurance"] = 2;
    task2->rewards["intellect"] = 30;
    task2->Load();
    tasks.push_back(task2);
    
    std::shared_ptr<GameTask> task3 = std::make_shared<GameTask>();
    task3->id = 3;
    task3->cost = 30;
    task3->title = L"跑步 40 km";
    task3->desc = L"简单跑个 40 km";
    task3->requirements["strength"] = 2;
    task3->requirements["endurance"] = 2;
    task3->rewards["speed"] = 30;
    task3->Load();
    tasks.push_back(task3);
    
    std::shared_ptr<GameTask> task4 = std::make_shared<GameTask>();
    task4->id = 4;
    task4->cost = 3600;
    task4->title = L"个人演唱会";
    task4->desc = L"举办轴伊的首次个人演唱会";
    task4->requirements["speed"] = 20;
    task4->requirements["endurance"] = 20;
    task4->requirements["strength"] = 20;
    task4->requirements["will"] = 20;
    task4->requirements["intellect"] = 20;
    task4->rewards["exp"] = 5000;
    task4->special = std::make_shared<SpecialReward>(L"礼服", L"解锁礼服",
                                                     "clothes.1.active");
    task4->Load();
    tasks.push_back(task4);
    
    std::shared_ptr<GameTask> task5 = std::make_shared<GameTask>();
    task5->id = 5;
    task5->cost = 7200;
    task5->title = L"圣诞节约会";
    task5->desc = L"「今、今天、是约定好的日子呢……！」";
    task5->requirements["speed"] = 50;
    task5->requirements["endurance"] = 50;
    task5->requirements["strength"] = 50;
    task5->requirements["will"] = 50;
    task5->requirements["intellect"] = 50;
    task5->rewards["exp"] = 10000;
    task5->special = std::make_shared<SpecialReward>(L"冬装", L"解锁冬装",
                                                     "clothes.2.active");
    task5->Load();
    tasks.push_back(task5);
    
    std::shared_ptr<GameTask> task6 = std::make_shared<GameTask>();
    task6->id = 6;
    task6->cost = 3600;
    task6->title = L"全能之主";
    task6->desc = L"所有属性达到 100 点";
    task6->requirements["speed"] = 100;
    task6->requirements["endurance"] = 100;
    task6->requirements["strength"] = 100;
    task6->requirements["will"] = 100;
    task6->requirements["intellect"] = 100;
    task6->rewards["exp"] = 50000;
    task6->Load();
    tasks.push_back(task6);
    
    std::shared_ptr<GameTask> taskDebug = std::make_shared<GameTask>();
    taskDebug->id = 999;
    taskDebug->cost = 10;
    taskDebug->title = L"白给";
    taskDebug->desc = L"";
    taskDebug->requirements["speed"] = 0;
    taskDebug->rewards["speed"] = 100;
    taskDebug->rewards["endurance"] = 100;
    taskDebug->rewards["strength"] = 100;
    taskDebug->rewards["will"] = 100;
    taskDebug->rewards["intellect"] = 100;
    taskDebug->rewards["exp"] = 500000;
    taskDebug->Load();
    tasks.push_back(taskDebug);
    
    return tasks;
  }
};
