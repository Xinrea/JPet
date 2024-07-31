#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <time.h>

#include "LAppPal.hpp"

using std::map;
using std::wstring;
using std::string;

class GameTask {
 public:
  int id;
  time_t start_time;
  int cost;
  wstring title = L"";
  wstring desc = L"";
  bool success;
  bool done;
  map<string, int> requirements;
  map<string, int> rewards;

  GameTask() = default;

  void Load();

  bool Done();

  static std::vector<std::shared_ptr<GameTask>> InitTasks() {
    std::vector<std::shared_ptr<GameTask>> tasks;
    // create tasks
    std::shared_ptr<GameTask> task1 = std::make_shared<GameTask>();
    task1->id = 1;
    task1->cost = 60;
    task1->title = L"拧瓶盖";
    task1->desc = L"使用全身力气拧开瓶盖";
    task1->requirements["strength"] = 1;
    task1->rewards["will"] = 10;
    task1->Load();
    tasks.push_back(task1);

    std::shared_ptr<GameTask> task2 = std::make_shared<GameTask>();
    task2->id = 2;
    task2->cost = 120;
    task2->title = L"1km跑步";
    task2->desc = L"简单跑个1km";
    task2->requirements["strength"] = 2;
    task2->requirements["endurance"] = 2;
    task2->rewards["intellect"] = 30;
    task2->Load();
    tasks.push_back(task2);
    return tasks;
  }
};
