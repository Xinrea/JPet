#pragma once

#include <memory>
#include <mutex>

#include "Task.hpp"


/**
 * @brief TaskScheduler manages a list of tasks and executes them in a separate thread.
 * such as game exp and game task timer etc.
 */
class TaskScheduler {
  public:
    ~TaskScheduler();

    static TaskScheduler* GetInstance() {
      static TaskScheduler instance;
      return &instance;
    }

    void AddTask(std::shared_ptr<Task> task);
    void RemoveTask(std::shared_ptr<Task> task);

  private:
    TaskScheduler();
    std::vector<std::shared_ptr<Task>> _tasks;
    std::mutex _mutex;
    std::thread _worker;
    bool _running = true;
    void doRun();
    void tick();
};
