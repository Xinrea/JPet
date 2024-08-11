#pragma once
#include <vector>
#include <string>

#include "LAppPal.hpp"
#include "httplib.h"

// BuffManager manages buff status. buffs are stored in memory, no need to persist.
class BuffManager {
private:
  time_t latest_ = 0;
  bool is_live_ = false;
  bool is_dynamic_ = false;
  bool is_guard_ = false;
  int medal_level_ = 0;

  bool running_ = true;
  std::thread worker_;

  void updateDynamic(const httplib::Headers& headers);
  void updateLive(const httplib::Headers& headers);
  void updateGuard(const httplib::Headers& headers);

  void thread();
  
public:
  static BuffManager* GetInstance() {
    static BuffManager instance;
    return &instance;
  }

  BuffManager() { worker_ = std::thread(&BuffManager::thread, this); }

  ~BuffManager() {
    running_ = false;
    worker_.join();
  }

  std::vector<std::string> GetBuffList() {
    std::vector<std::string> buffs;
    if (is_live_) {
      buffs.push_back("live");
    }
    if (is_dynamic_) {
      buffs.push_back("dynamic");
    }
    if (is_guard_) {
      buffs.push_back("guard");
    }
    return buffs;
  }

  void Update();

  bool IsLive() {
    return is_live_;
  }

  bool IsDynamic() {
    return is_dynamic_;
  }

  bool IsGuard() {
    return is_guard_;
  }

  int MedalLevel() {
    return medal_level_;
  }
};
