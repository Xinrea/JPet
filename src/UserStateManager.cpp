#include "UserStateManager.h"
#include "LAppDefine.hpp"
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif

#include <httplib.h>

bool UserStateManager::CheckUpdate() {
  httplib::SSLClient liveCli("pet.vjoi.cn", 443);
  liveCli.set_follow_location(true);
  liveCli.set_connection_timeout(std::chrono::seconds(1));
  auto res = liveCli.Get("/version.txt");
  if (res && res->status == 200) {
    if (LAppDefine::DebugLogEnable)
      LAppPal::PrintLog(
          (std::string("[UserStateWatcher]Check Update Latest: ") + res->body)
              .c_str());
    int now = _wtoi(WVERSION);
    int latest = atoi(res->body.c_str());
    if (now < latest) {
      LAppPal::PrintLog("[UserStateWatcher]Need Update: %s -> %s ", VERSION,
                        res->body.c_str());
      return true;
    } else
      return false;
  } else {
    if (LAppDefine::DebugLogEnable) {
      LAppPal::PrintLog("[UserStateWatcher]Check Update Failed");
      if (res) LAppPal::PrintLog(res->body.c_str());
    }
  }
  return false;
}