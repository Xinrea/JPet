#pragma once

#include <croncpp.h>

#include "DataManager.hpp"
#include "LAppPal.hpp"

class Task {
 public:
  virtual bool ShouldExecute() = 0;
  virtual void Execute() = 0;
  virtual bool IsDone() = 0;
  virtual void SetDone() = 0;
  virtual ~Task() {}
};

class ExpTask : public Task {
 public:
  bool ShouldExecute() override {
    static std::time_t next = cron::cron_next(_cron, std::time(0));
    std::time_t now = std::time(0);
    if (now != next) {
      return false;
    }
    next = cron::cron_next(_cron, now);
    return true;
  }
  void Execute() override { DataManager::GetInstance()->AddExp(_bonus); }
  bool IsDone() override {
    // ExpTask never done
    return false;
  }
  void SetDone() override {}
  void SetBonus(bool bonus) { _bonus = bonus; }

 private:
  bool _bonus = false;
  cron::cronexpr _cron = cron::make_cron("0 * * * * *");
};
