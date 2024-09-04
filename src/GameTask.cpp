#include "GameTask.hpp"
#include "DataManager.hpp"
#include "LAppDefine.hpp"

using namespace WinToastLib;

void GameTask::Load() {
  auto status_vec = DataManager::GetInstance()->TaskStatus(id);
  start_time = status_vec[0];
  end_time = status_vec[1];
  success = status_vec[2] == 1;
  status = static_cast<TStatus>(status_vec[3]);
  cost_snapshot = status_vec[4];
}

void GameTask::Dump() {
  DataManager::GetInstance()->DumpTask(id, start_time, end_time, success, static_cast<int>(status), cost_snapshot);
}

int GameTask::GetCurrentCost() {
  int speed = DataManager::GetInstance()->GetAttribute("speed");
  return cost * (1 - 0.75 * LAppPal::EaseOut(speed - 2) / 100);
}

void GameTask::Notify(const wstring& title, const wstring& content,
                              WinToastEventHandler* handler) {
  WinToastTemplate templ = WinToastTemplate(WinToastTemplate::ImageAndText02);
  // convert char* to wstring
  templ.setTextField(title, WinToastTemplate::FirstLine);
  templ.setTextField(content, WinToastTemplate::SecondLine);
  std::wstring img = LAppDefine::execPath + std::wstring(L"resources/imgs/Avatar.png");
  templ.setImagePath(img);
  WinToast::instance()->showToast(templ, handler, nullptr);
}

void GameTask::TryDone() {
  // task not running now
  if (status != TStatus::RUNNING) {
    return;
  }
  // compare start time + cost with current time
  time_t current_time = time(NULL);
  bool state = current_time >= start_time + cost_snapshot;
  // time's up, set status now
  if (state) {
    // check success or not
    int lack = 0;
    for (auto it = requirements.begin(); it != requirements.end(); ++it) {
      // get attribute from game data
      int value = DataManager::GetInstance()->GetAttribute(it->first);
      if (value < it->second) {
        // if lack attribute
        lack += it->second - value;
      }
    }
    srand(time(NULL));

    // get willpower
    int will = DataManager::GetInstance()->GetAttribute("will");
    // every lack of attribute will reduce 3% -> 12
    // every will will increase 0.25% -> 1
    lack *= 12;
    // if lack is 0, the full rate is 70%, lack 30% -> 120
    int starcnt = DataManager::GetInstance()->GetWithDefault("starcnt", 0);
    lack += 120 + 20 * starcnt;
    if (lack >= 400) {
      success = false;
      LAppPal::PrintLog(LogLevel::Info, "[GameTask]Task %d failed before will takes effect", id);
    } else {
      lack -= will;
      // max rate is 95%, 5% -> 20
      if (lack < 20) {
        lack = 20;
      }
      // using random number to determine success
      int random = rand() % 400;
      if (random < lack) {
        success = false;
        LAppPal::PrintLog(LogLevel::Info, "[GameTask]Task %d failed", id);
      } else {
        success = true;
        LAppPal::PrintLog(LogLevel::Info, "[GameTask]Task %d success", id);
      }
      status = TStatus::WAIT_SETTLE;
      Notify(L"任务完成", title, new WinToastEventHandler("TASK_COMPLETE"));
    }

    Dump();
  }
}
