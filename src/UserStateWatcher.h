#include <memory>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "LAppPal.hpp"
#include "BilibiliDynamic.hpp"
#include "StateMessage.hpp"
#include "Wbi.hpp"

using std::queue;
using std::string;

enum class CheckStatus {
 SUCCESS, FAST
};

class UserStateWatcher {
 public:
  WatchTarget target;
  bool lastStatus = false;
  std::vector<std::string> dynamic_ids;
  bool dynamic_initialized = false;
  int64_t dynamic_latest_time = 0;
  time_t dynamic_next_check_at = 0;
  time_t dynamic_retry_at = 0;
  time_t wbi_retry_at = 0;
  UserStateWatcher(const string& uid,
                   const string& userAgent, shared_ptr<WbiConfig> wbi_config);
  CheckStatus Check(queue<StateMessage>& messageQueue, const string& cookies);

 private:
  bool _initialized = false;
  string _userAgent;
  shared_ptr<WbiConfig> _wbi_config;
  void checkDynamic(queue<StateMessage>& messageQueue, const string& cookies);
  void initBasicInfo(const string& cookies);
};
