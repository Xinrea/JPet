#pragma once
#include <string>
using namespace std;

struct WatchTarget {
  string uid;
  string roomid;
  string uname;
  string roomtitle;
};

enum MessageType {
  LiveMessage,
  DynamicMessage,
};

class StateMessage {
 public:
  MessageType type;
  WatchTarget target;
  string extra1;
  string extra2;
  StateMessage(MessageType tp, const WatchTarget& t, const string& ex1,
               const string& ex2) {
    type = tp;
    target = t;
    extra1 = ex1;
    extra2 = ex2;
  }
};
