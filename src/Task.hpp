#pragma once

#include <croncpp.h>

#include "DataManager.hpp"

class Task {
 public:
  virtual bool ShouldExecute() = 0;
  virtual void Execute() = 0;
  virtual bool IsDone() = 0;
  virtual void SetDone() = 0;
  virtual ~Task() {}
};
