﻿#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "LAppPal.hpp"
#include "StateMessage.hpp"

using std::queue;
using std::string;

enum class CheckStatus {
 SUCCESS, FAST, RESTRICT
};

class UserStateWatcher {
 public:
  WatchTarget target;
  bool lastStatus = false;
  long long lastTime = 0;
  UserStateWatcher(const string& uid, const string& cookies,
                   const string& userAgent);
  CheckStatus Check(queue<StateMessage>& messageQueue);

 private:
  bool _initialized = false;
  const string& _cookies;
  const string& _userAgent;
  string img_key, sub_key;
  void initBasicInfo();
};
