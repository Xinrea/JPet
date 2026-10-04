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


  void Notify(const wstring& title, const wstring& content,
                              WinToastEventHandler* handler);

};
