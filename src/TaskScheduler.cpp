#include "TaskScheduler.hpp"
#include "LAppPal.hpp"
#include <memory>

TaskScheduler::TaskScheduler() {
  _worker = std::thread(&TaskScheduler::doRun, this);
}

TaskScheduler::~TaskScheduler() {
  _running = false;
  _mutex.lock();
  _tasks.clear();
  _mutex.unlock();
  // wait for worker thread
  _worker.join();
}

void TaskScheduler::AddTask(std::shared_ptr<Task> task) {
  std::lock_guard<std::mutex> lock(_mutex);
  _tasks.push_back(task);
}

void TaskScheduler::RemoveTask(std::shared_ptr<Task> task) {
  std::lock_guard<std::mutex> lock(_mutex);
  auto it = std::find(_tasks.begin(), _tasks.end(), task);
  if (it != _tasks.end()) {
    _tasks.erase(it);
  }
}

void TaskScheduler::doRun() {
  LAppPal::PrintLog(LogLevel::Info, "[TaskScheduler]Worker running %d", std::this_thread::get_id());
  while (_running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    tick();
  }
  LAppPal::PrintLog(LogLevel::Info, "[TaskScheduler]Worker exit");
}

void TaskScheduler::tick() {
  std::lock_guard<std::mutex> lock(_mutex);
  for (auto it = _tasks.begin(); it != _tasks.end();) {
    if (!_running) {
      break;
    }
    std::shared_ptr<Task> task = *it;
    if (task->IsDone()) {
      it = _tasks.erase(it);
      continue;
    }
    if (task->ShouldExecute()) {
      task->Execute();
      task->SetDone();
    }
    ++it;
  }
}
