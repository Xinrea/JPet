#pragma once
#include <string>
using namespace std;

class StateMessage {
 public:
  string uid;
  string roomid;
  wstring name;
  wstring title;
  string content;
  StateMessage(string u, string r, wstring n, wstring t, string c) {
    uid = u;
    roomid = r;
    name = n;
    title = t;
    content = c;
  }
};
