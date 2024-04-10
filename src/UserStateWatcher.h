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
  UserStateWatcher(string list) {
    uidlist = split(list, ";");
    for (int i = 0; i < uidlist.size(); i++) {
      lastDynamic.push_back(0);
      lastLive.push_back(false);
    }
  }
  void Watch();
  static bool CheckUpdate();
  std::thread WatchThread();

  vector<string> split(const string& str, const string& delim) {
    vector<string> res;
    if ("" == str) return res;
    char* strs = new char[str.length() + 1];
    strcpy(strs, str.c_str());

    char* d = new char[delim.length() + 1];
    strcpy(d, delim.c_str());

    char* p = strtok(strs, d);
    while (p) {
      string s = p;
      res.push_back(s);
      p = strtok(NULL, d);
    }

    return res;
  };

 private:
  wstring StringToWString(const std::string& str);
};