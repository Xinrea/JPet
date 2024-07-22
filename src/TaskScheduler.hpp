#pragma once

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

    void AddTask(Task* task);
    void RemoveTask(Task* task);

  private:
    TaskScheduler();
    std::vector<Task*> _tasks;
    std::mutex _mutex;
    std::thread _worker;
    bool _running = true;
    void doRun();
    void tick();
};
