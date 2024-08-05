﻿#pragma once

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
    task1->desc = L"使用全身力气拧开瓶盖";
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
    task4->cost = 60;
    task4->title = L"举办演唱会";
    task4->desc = L"演唱会";
    task4->requirements["strength"] = 20;
    task4->requirements["endurance"] = 30;
    task4->requirements["intellect"] = 10;
    task4->rewards["exp"] = 300;
    task4->special = std::make_shared<SpecialReward>(L"礼服", L"解锁礼服衣装",
                                                     "clothes.1.active");
    task4->Load();
    tasks.push_back(task4);
    
    return tasks;
  }
};