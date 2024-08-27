#include <memory>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "LAppPal.hpp"
#include "StateMessage.hpp"
#include "Wbi.hpp"

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
  UserStateWatcher(const string& uid,
                   const string& userAgent, shared_ptr<WbiConfig> wbi_config);
  CheckStatus Check(queue<StateMessage>& messageQueue, const string& cookies);

 private:
  bool _initialized = false;
  const string& _userAgent;
  shared_ptr<WbiConfig> _wbi_config;
  void initBasicInfo(const string& cookies);
};
