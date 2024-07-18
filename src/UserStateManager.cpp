#include "UserStateManager.h"
#include "LAppDefine.hpp"

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif

#include <httplib.h>

using namespace WinToastLib;

void UserStateManager::CheckUpdate() {
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
      Notify(L"检测到新版本", L"请前往下载",
             new WinToastEventHandler("https://pet.vjoi.cn"));
    }
  } else {
    LAppPal::PrintLog(LogLevel::Error, "[UserStateWatcher]Check Update Failed");
    if (res) LAppPal::PrintLog(res->body.c_str());
  }
}

void UserStateManager::Notify(const wstring& title, const wstring& content,
                              WinToastEventHandler* handler) {
  WinToastTemplate templ = WinToastTemplate(WinToastTemplate::ImageAndText02);
  // convert char* to wstring
  templ.setTextField(title, WinToastTemplate::FirstLine);
  templ.setTextField(content, WinToastTemplate::SecondLine);
  std::wstring img = _exePath + std::wstring(L"resources/imgs/Avatar.png");
  templ.setImagePath(img);
  WinToast::instance()->showToast(templ, handler, nullptr);
}
void UserStateManager::Init(const std::vector<std::string>& list, HWND parent) {
  // 通知初始化
  LAppPal::PrintLog(LogLevel::Info, "[LAppDelegate]Notification Init");
  WinToast::instance()->setAppName(L"JPet");
  const auto aumi =
      WinToast::configureAUMI(L"JoiGroup", L"JPetProject", L"JPet", WVERSION);
  WinToast::instance()->setAppUserModelId(aumi);
  WinToast::instance()->initialize();
  wchar_t curPath[256];
  GetModuleFileName(GetModuleHandle(NULL), static_cast<LPWSTR>(curPath),
                    sizeof(curPath));
  _exePath = std::wstring(curPath);

  // init cookie window
  _cookieWindow = new CookieWindow(parent, GetModuleHandle(nullptr));
  // running check thread
  _checkThread = std::thread(&UserStateManager::CheckThread, this, list);
  _checkThread.detach();
}

void UserStateManager::CheckThread(const vector<string>& list) {
  // sleep for 3 seconds to wait for cookie window
  std::this_thread::sleep_for(std::chrono::seconds(3));
  _mutex.lock();
  for (auto uid : list) {
    UserStateWatcher* watcher = new UserStateWatcher(uid, _cookieWindow->cookie,
                                                     _cookieWindow->userAgent);
    _watchers.push_back(watcher);
    // sleep for 5s to avoid 799 error
    std::this_thread::sleep_for(std::chrono::seconds(5));
  }
  _mutex.unlock();
  while (_running) {
    // check cookie
    if (_cookieWindow->cookie.empty()) {
      _cookieWindow->UpdateCookie();
    }
    // copy a shadow of _watchers
    _mutex.lock();
    std::vector<UserStateWatcher*> watchers = _watchers;
    _mutex.unlock();
    LAppPal::PrintLog("[UserStateManager]CheckThread running with %d watchers",
                      watchers.size());
    for (auto watcher : watchers) {
      bool cookieValid = watcher->Check(_messageQueue);
      // notify message process
      auto msg = FetchOne();
      if (msg.has_value()) {
        auto messageInfo = msg.value();
        auto wuname = LAppPal::StringToWString(messageInfo.target.uname);
        auto wroomtitle =
            LAppPal::StringToWString(messageInfo.target.roomtitle);
        if (messageInfo.type == MessageType::LiveMessage &&
            _liveNotifyEnabled) {
          Notify(wuname + L" - 直播中", wroomtitle,
                 new WinToastEventHandler("https://live.bilibili.com/" +
                                          messageInfo.target.roomid));
        }
        if (messageInfo.type == MessageType::DynamicMessage &&
            _dynamicNotifyEnabled) {
          auto wdesc = LAppPal::StringToWString(messageInfo.extra2);
          Notify(wuname + L" - 新动态", wdesc,
                 new WinToastEventHandler("https://t.bilibili.com/" +
                                          messageInfo.extra1));
        }
      }
      // sleep for 3 second
      std::this_thread::sleep_for(std::chrono::seconds(3));
      if (!cookieValid) {
        MessageBox(nullptr, L"获取动态信息失败，请在出现的窗口中点击完成验证码",
                   L"Error", MB_OK);
        _cookieWindow->Show();
        goto skip;
      }
    }
  skip:
    // sleep for 10 seconds
    std::this_thread::sleep_for(std::chrono::seconds(15));
  }
}