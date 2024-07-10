#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "LAppPal.hpp"
#include "StateMessage.hpp"

using std::queue;
using std::string;
using std::vector;

class UserStateWatcher {
 public:
  bool isExit = false;
  vector<bool> lastLive;
  vector<long long> lastDynamic;
  queue<StateMessage> newLive;
  queue<StateMessage> newDynamic;
  vector<string> uidlist;
  UserStateWatcher(const std::vector<std::string>& list) {
    uidlist = list;
    for (int i = 0; i < uidlist.size(); i++) {
      lastDynamic.push_back(0);
      lastLive.push_back(false);
    }
  }
  void Watch();
  static bool CheckUpdate();
  std::thread WatchThread();

 private:
  wstring StringToWString(const std::string& str);
};