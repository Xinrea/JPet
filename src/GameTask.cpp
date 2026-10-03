#include "GameTask.hpp"
#include "DataManager.hpp"
#include "LAppDefine.hpp"

#include <random>

#ifdef __APPLE__
#include "Platform.hpp"
#else
using namespace WinToastLib;
#endif

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
#ifdef __APPLE__
  std::unique_ptr<WinToastEventHandler> owned(handler);
  Platform::Notify(title, content, owned ? owned->GetUrl() : "");
#else
  WinToastTemplate templ = WinToastTemplate(WinToastTemplate::ImageAndText02);
  // convert char* to wstring
  templ.setTextField(title, WinToastTemplate::FirstLine);
  templ.setTextField(content, WinToastTemplate::SecondLine);
  std::wstring img = LAppDefine::execPath + std::wstring(L"resources/imgs/Avatar.png");
  templ.setImagePath(img);
  WinToast::instance()->showToast(templ, handler, nullptr);
#endif
}

double GameTask::SuccessRate() {
  auto dm = DataManager::GetInstance();
  int lack = 0;
  for (const auto& [key, required] : requirements) {
    lack += std::max(0, required - dm->GetAttribute(key));
  }
  lack = lack * 12 + 120 + 20 * dm->GetWithDefault("starcnt", 0);
  if (lack >= 400) return 0;
  lack = std::max(20, lack - dm->GetAttribute("will"));
  return (400 - lack) / 400.0;
}

void GameTask::TryDone(time_t now) {
  if (status != TStatus::RUNNING || now < start_time + cost_snapshot) return;

  static thread_local std::mt19937 random(std::random_device{}());
  std::uniform_int_distribution<int> roll(0, 399);
  success = roll(random) >= static_cast<int>(std::lround((1 - SuccessRate()) * 400));
  // Even a guaranteed failure must settle, otherwise it blocks the whole queue.
  status = TStatus::WAIT_SETTLE;
  Dump();
}
