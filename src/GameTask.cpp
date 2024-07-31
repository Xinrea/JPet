#include "GameTask.hpp"
#include "DataManager.hpp"

void GameTask::Load() {
  auto status = DataManager::GetInstance()->TaskStatus(id);
  start_time = status[0];
  success = status[1] == 1;
  done = status[2] == 1;
}

bool GameTask::Done() {
  // not even started
  if (start_time == 0) {
    return false;
  }
  if (success) {
    return true;
  }
  // compare start time + cost with current time
  time_t current_time = time(NULL);
  if (done) {
    return true;
  }
  bool state = current_time >= start_time + cost;
  if (state) {
    done = true;
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
    // if lack attribute for 20 or more
    if (lack >= 20) {
      // immediately fail
      success = false;
    } else {
      // get willpower
      int will = DataManager::GetInstance()->GetAttribute("will");
      // every lack of attribute will reduce 2% -> 4
      // every will will increase 0.5% -> 1
      lack *= 4;
      lack -= will;
      if (lack < 0) {
        lack = 0;
      }
      // using random number to determine success
      int random = rand() % 200;
      if (random < lack) {
        success = false;
        LAppPal::PrintLog(LogLevel::Info, "[GameTask]Task %d failed", id);
      } else {
        success = true;
        // update attribute
        for (auto it = rewards.begin(); it != rewards.end(); ++it) {
          DataManager::GetInstance()->AddAttribute(it->first, it->second);
        }
        LAppPal::PrintLog(LogLevel::Info, "[GameTask]Task %d success", id);
      }
    }
    DataManager::GetInstance()->DumpTask(id, start_time, success, done);
  }
  return done;
}
