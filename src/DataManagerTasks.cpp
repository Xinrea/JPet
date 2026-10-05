#include "DataManager.hpp"
#include "CloudGame.hpp"

nlohmann::json DataManager::GetTaskState() {
  auto state = GetCloudSnapshot();
  if (!state.is_null()) return state["tasks"];
  return {{"current", nullptr}, {"list", nlohmann::json::array()},
    {"queue", nlohmann::json::array()}, {"queue_capacity", 2},
    {"history", nlohmann::json::array()}, {"queue_blocked", false}, {"online", false}};
}
std::shared_ptr<GameTask> DataManager::GetCurrentTask() {
  const auto state = GetTaskState();
  if (state["current"].is_null()) return nullptr;
  const auto& item = state["current"];
  auto task = std::make_shared<GameTask>();
  task->id = item.at("id").get<int>();
  task->title = LAppPal::StringToWString(item.at("title").get<std::string>());
  task->desc = LAppPal::StringToWString(item.at("desc").get<std::string>());
  task->cost = task->cost_snapshot = item.at("cost").get<int>();
  task->start_time = time(nullptr) - static_cast<time_t>(item.value("elapsed_seconds", 0.0));
  task->status = TStatus::RUNNING;
  task->success = false;
  return task;
}
int DataManager::TaskQueueCapacity() { return GetTaskState().value("queue_capacity", 2); }
std::string DataManager::UpgradeTaskQueue() { return CloudGame::GetInstance()->Command({{"type", "queue.upgrade"}}); }
std::string DataManager::StartTask(int id, time_t) { return CloudGame::GetInstance()->Command({{"type", "task.start"}, {"id", id}}); }
std::string DataManager::QueueTask(int id, time_t) { return CloudGame::GetInstance()->Command({{"type", "task.queue"}, {"id", id}}); }
std::string DataManager::CancelTask(int id, time_t) { return CloudGame::GetInstance()->Command({{"type", "task.cancel"}, {"id", id}}); }
std::string DataManager::RemoveQueuedTask(int64_t id) { return CloudGame::GetInstance()->Command({{"type", "queue.remove"}, {"entry_id", id}}); }
std::string DataManager::MoveQueuedTask(int64_t id, int direction) { return CloudGame::GetInstance()->Command({{"type", "queue.move"}, {"entry_id", id}, {"direction", direction}}); }
