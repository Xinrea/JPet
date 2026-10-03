#include "DataManager.hpp"

#include "PanelServer.hpp"
#include <algorithm>

namespace {
nlohmann::json DescribeTask(const std::shared_ptr<GameTask>& task, bool running) {
  nlohmann::json result = {
      {"id", task->id}, {"title", LAppPal::WStringToString(task->title)},
      {"desc", LAppPal::WStringToString(task->desc)},
      {"start_time", task->start_time}, {"end_time", task->end_time},
      {"cost", running ? task->cost_snapshot : task->cost},
      {"success", task->success}, {"status", task->status},
      {"repeatable", task->repeatable}, {"requirements", task->requirements},
      {"rewards", task->rewards}, {"rate", task->SuccessRate() * 100}};
  if (task->special) {
    result["special"] = {{"title", LAppPal::WStringToString(task->special->title)},
                         {"desc", LAppPal::WStringToString(task->special->desc)}};
  }
  return result;
}
}

void DataManager::LoadTasks() {
  if (!tasks.empty()) return;
  tasks = GameTask::InitTasks();
  try {
    auto saved = nlohmann::json::parse(GetWithDefault("task.queue", std::string{"{}"}));
    nextQueueId = std::max<int64_t>(1, saved.value("next_id", int64_t{1}));
    for (const auto& entry : saved.value("entries", nlohmann::json::array())) {
      auto task = FindTask(entry.at("task_id").get<int>());
      auto entryId = entry.at("entry_id").get<int64_t>();
      if (!task || task->status == TStatus::ARCHIVED || entryId < 1) continue;
      taskQueue.push_back(entry);
      nextQueueId = std::max(nextQueueId, entryId + 1);
    }
  } catch (const std::exception& e) {
    taskQueue = nlohmann::json::array();
    LAppPal::PrintLog(LogLevel::Warn, "[Tasks]Discard invalid queue: %s", e.what());
  }
  try {
    auto saved = nlohmann::json::parse(GetWithDefault("task.history", std::string{"[]"}));
    if (saved.is_array()) taskHistory = saved;
  } catch (const std::exception& e) {
    LAppPal::PrintLog(LogLevel::Warn, "[Tasks]Discard invalid history: %s", e.what());
  }
}

std::shared_ptr<GameTask> DataManager::FindTask(int id) {
  for (auto& task : tasks) if (task->id == id) return task;
  return nullptr;
}

std::shared_ptr<GameTask> DataManager::GetCurrentTask() {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  LoadTasks();
  for (auto& task : tasks) {
    if (task->status == TStatus::RUNNING || task->status == TStatus::WAIT_SETTLE) {
      // The renderer receives a snapshot, never a concurrently mutated task.
      return std::make_shared<GameTask>(*task);
    }
  }
  return nullptr;
}

int DataManager::TaskQueueCapacity() {
  return 2 + std::max(0, GetWithDefault("starcnt", 0));
}

void DataManager::SaveTaskQueue() {
  gameData->UpdateBatch({}, {{"task.queue", nlohmann::json{
      {"next_id", nextQueueId}, {"entries", taskQueue}}.dump()}});
}

nlohmann::json DataManager::GetTaskState() {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  LoadTasks();
  auto current = GetCurrentTask();
  nlohmann::json result = {{"current", nullptr}, {"list", nlohmann::json::array()},
      {"queue", nlohmann::json::array()}, {"queue_capacity", TaskQueueCapacity()},
      {"history", taskHistory}, {"queue_blocked", false}};
  for (auto& task : tasks) {
    if (current && current->id == task->id) result["current"] = DescribeTask(task, true);
    else result["list"].push_back(DescribeTask(task, false));
  }
  for (const auto& entry : taskQueue) {
    auto task = FindTask(entry.at("task_id").get<int>());
    if (!task) continue;
    auto description = DescribeTask(task, false);
    description["entry_id"] = entry.at("entry_id");
    result["queue"].push_back(description);
  }
  if (!current && !result["queue"].empty()) {
    result["queue_blocked"] = result["queue"][0]["rate"].get<double>() == 0;
  }
  return result;
}

std::string DataManager::StartTask(int id, time_t now) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  LoadTasks();
  auto task = FindTask(id);
  if (!task) return "任务不存在";
  if (GetCurrentTask() || !taskQueue.empty()) return "已有任务在运行或排队，请加入队列";
  if (task->status == TStatus::ARCHIVED) return "该任务已经完成";
  if (task->SuccessRate() == 0) return "当前属性不足，任务成功率为 0";
  const auto prefix = "task." + std::to_string(id) + ".";
  int duration = task->GetCurrentCost();
  gameData->UpdateBatch({{prefix + "start_time", static_cast<int>(now)},
      {prefix + "success", 0}, {prefix + "status", static_cast<int>(TStatus::RUNNING)},
      {prefix + "cost_snapshot", duration}, {prefix + "queued", 0}});
  task->start_time = now;
  task->success = false;
  task->status = TStatus::RUNNING;
  task->cost_snapshot = duration;
  PanelServer::GetInstance()->Notify("UPDATE");
  return {};
}

std::string DataManager::QueueTask(int id, time_t now) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  LoadTasks();
  auto task = FindTask(id);
  if (!task) return "任务不存在";
  if (task->status == TStatus::ARCHIVED) return "该任务已经完成";
  if (!task->repeatable) {
    if (task->status == TStatus::RUNNING || task->status == TStatus::WAIT_SETTLE) {
      return "该任务正在执行";
    }
    for (const auto& entry : taskQueue) {
      if (entry["task_id"] == id) return "该任务已在队列中";
    }
  }
  if (taskQueue.size() >= static_cast<size_t>(TaskQueueCapacity())) return "任务队列已满，升星可增加容量";
  auto previous = taskQueue;
  taskQueue.push_back({{"entry_id", nextQueueId++}, {"task_id", id}});
  try { SaveTaskQueue(); } catch (...) { taskQueue = previous; throw; }
  StartNextQueuedTask(now);
  PanelServer::GetInstance()->Notify("UPDATE");
  return {};
}

void DataManager::StartNextQueuedTask(time_t now) {
  if (GetCurrentTask()) return;
  while (!taskQueue.empty()) {
    auto task = FindTask(taskQueue[0]["task_id"].get<int>());
    if (!task || task->status == TStatus::ARCHIVED) {
      taskQueue.erase(taskQueue.begin());
      SaveTaskQueue();
      continue;
    }
    // A queued task may become eligible after earlier training. Keep its place
    // if it is still impossible, so the player can reorder or remove it.
    if (task->SuccessRate() == 0) return;
    auto remaining = taskQueue;
    remaining.erase(remaining.begin());
    const auto prefix = "task." + std::to_string(task->id) + ".";
    int duration = task->GetCurrentCost();
    gameData->UpdateBatch({{prefix + "start_time", static_cast<int>(now)},
        {prefix + "success", 0}, {prefix + "status", static_cast<int>(TStatus::RUNNING)},
        {prefix + "cost_snapshot", duration}, {prefix + "queued", 1}}, {{"task.queue", nlohmann::json{
            {"next_id", nextQueueId}, {"entries", remaining}}.dump()}});
    taskQueue = remaining;
    task->start_time = now;
    task->success = false;
    task->status = TStatus::RUNNING;
    task->cost_snapshot = duration;
    PanelServer::GetInstance()->Notify("UPDATE");
    return;
  }
}

bool DataManager::SettleTask(const std::shared_ptr<GameTask>& task, time_t now) {
  if (task->status != TStatus::WAIT_SETTLE) return false;
  LoadAchievements();
  std::map<std::string, int> updates;
  auto actualRewards = task->success ? task->rewards : std::map<std::string, int>{};
  if (task->success) {
    int experience = GetAttribute("exp");
    int previousExperience = experience;
    for (const auto& [key, reward] : task->rewards) {
      if (key == "exp") experience = std::min(99999999, experience + reward);
      else {
        int previous = GetAttribute(key);
        int value = std::min(99999999, std::max(0, previous + reward));
        if (value > GetAttrLimit()) {
          experience = std::min(99999999, experience + (value - GetAttrLimit()) * 26500);
          value = GetAttrLimit();
        }
        updates["attr." + key] = value;
        actualRewards[key] = value - previous;
      }
    }
    if (task->id == 1) {
      actualRewards["exp"] = 10 * CurrentExpDiff();
      experience = std::min(99999999, experience + actualRewards["exp"]);
    }
    updates["attr.exp"] = experience;
    actualRewards["exp"] = experience - previousExperience;
    for (auto it = actualRewards.begin(); it != actualRewards.end();) {
      if (it->second == 0) it = actualRewards.erase(it);
      else ++it;
    }
    if (task->special) updates[task->special->linked_key] = 1;
    updates["buff.failcount"] = 0;
  } else {
    updates["buff.failcount"] = GetWithDefault("buff.failcount", 0) + 1;
  }
  auto status = !task->repeatable && task->success ? TStatus::ARCHIVED : TStatus::IDLE;
  auto prefix = "task." + std::to_string(task->id) + ".";
  updates[prefix + "end_time"] = static_cast<int>(now);
  updates[prefix + "status"] = static_cast<int>(status);
  auto history = taskHistory;
  auto completed = DescribeTask(task, true);
  completed["end_time"] = now;
  completed["rewards"] = actualRewards;
  completed["status"] = status;
  if (!task->success) completed.erase("special");
  history.insert(history.begin(), completed);
  if (history.size() > 10) history.erase(history.begin() + 10, history.end());
  // Commit the rewards and settled status atomically to prevent double rewards
  // if the app exits before advancing the queue.
  auto nextAchievements = achievementState;
  Achievements::Task(nextAchievements, task->id, task->success,
      GetWithDefault(prefix + "queued", 0) == 1);
  Achievements::Observe(nextAchievements, AchievementSnapshot(updates));
  auto unlocked = Achievements::Evaluate(nextAchievements, now);
  gameData->UpdateBatch(updates, {{"task.history", history.dump()},
      {"achievements.state", nextAchievements.dump()}});
  achievementState = nextAchievements;
  taskHistory = history;
  task->end_time = now;
  task->status = status;
  NotifyAchievements(unlocked);
  task->Notify(task->success ? L"任务成功，奖励已发放" : L"任务失败，已自动结算",
               task->title, new WinToastEventHandler("TASK_COMPLETE"));
  return true;
}

void DataManager::TickTasks(time_t now) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  try {
    LoadTasks();
    bool settled = false;
    for (auto& task : tasks) {
      task->TryDone(now);
      settled = SettleTask(task, now) || settled;
    }
    StartNextQueuedTask(now);
    if (settled) PanelServer::GetInstance()->Notify("UPDATE");
  } catch (const std::exception& e) {
    LAppPal::PrintLog(LogLevel::Error, "[Tasks]Transition failed: %s", e.what());
  }
}

std::string DataManager::RemoveQueuedTask(int64_t entryId) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  LoadTasks();
  auto remaining = taskQueue;
  auto entry = std::find_if(remaining.begin(), remaining.end(), [&](const auto& item) {
    return item["entry_id"] == entryId;
  });
  if (entry == remaining.end()) return "该排队任务已开始或已被移除";
  remaining.erase(entry);
  gameData->UpdateBatch({}, {{"task.queue", nlohmann::json{
      {"next_id", nextQueueId}, {"entries", remaining}}.dump()}});
  taskQueue = remaining;
  StartNextQueuedTask(time(nullptr));
  PanelServer::GetInstance()->Notify("UPDATE");
  return {};
}

std::string DataManager::MoveQueuedTask(int64_t entryId, int direction) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  LoadTasks();
  if (direction != -1 && direction != 1) return "无效的移动方向";
  auto reordered = taskQueue;
  for (size_t i = 0; i < reordered.size(); ++i) {
    if (reordered[i]["entry_id"] != entryId) continue;
    int next = static_cast<int>(i) + direction;
    if (next < 0 || next >= static_cast<int>(reordered.size())) return "已到达队列边界";
    std::swap(reordered[i], reordered[next]);
    gameData->UpdateBatch({}, {{"task.queue", nlohmann::json{
        {"next_id", nextQueueId}, {"entries", reordered}}.dump()}});
    taskQueue = reordered;
    StartNextQueuedTask(time(nullptr));
    PanelServer::GetInstance()->Notify("UPDATE");
    return {};
  }
  return "该排队任务已开始或已被移除";
}

std::string DataManager::CancelTask(int id, time_t now) {
  std::lock_guard<std::recursive_mutex> lock(gameMutex);
  LoadTasks();
  auto task = FindTask(id);
  if (!task || task->status != TStatus::RUNNING) return "该任务当前未在运行";
  auto prefix = "task." + std::to_string(id) + ".";
  gameData->UpdateBatch({{prefix + "start_time", 0},
      {prefix + "status", static_cast<int>(TStatus::IDLE)}});
  task->start_time = 0;
  task->status = TStatus::IDLE;
  StartNextQueuedTask(now);
  PanelServer::GetInstance()->Notify("UPDATE");
  return {};
}
