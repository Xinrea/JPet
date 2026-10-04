#include "UserStateManager.h"
#include "DataManager.hpp"
#include "LAppDefine.hpp"
#include "LAppPal.hpp"

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif

#include <httplib.h>
#include <cstdlib>

#ifdef __APPLE__
#include "Platform.hpp"
#else
using namespace WinToastLib;
#endif

void UserStateManager::Notify(const wstring& title, const wstring& content,
                              WinToastEventHandler* handler) {
#ifdef __APPLE__
  std::unique_ptr<WinToastEventHandler> owned(handler);
  Platform::Notify(title, content, handler ? handler->GetUrl() : "");
#else
  WinToastTemplate templ = WinToastTemplate(WinToastTemplate::ImageAndText02);
  // convert char* to wstring
  templ.setTextField(title, WinToastTemplate::FirstLine);
  templ.setTextField(content, WinToastTemplate::SecondLine);
  std::wstring img = LAppDefine::execPath + std::wstring(L"resources/imgs/Avatar.png");
  templ.setImagePath(img);
  WinToast::instance()->showToast(templ, handler, nullptr);
#endif
}

#ifdef __APPLE__
void UserStateManager::Init(const std::vector<std::string>& list, void* parent) {
  if (std::getenv("JPET_SMOKE_TEST")) {
    _cookieWindow = new CookieWindow(parent);
    return;
  }
#else
void UserStateManager::Init(const std::vector<std::string>& list, HWND parent) {
  // 通知初始化
  LAppPal::PrintLog(LogLevel::Info, "[LAppDelegate]Notification Init");
  WinToast::instance()->setAppName(L"JPet");
  const auto aumi =
      WinToast::configureAUMI(L"JoiGroup", L"JPetProject", L"JPet", LAppPal::StringToWString(VERSION));
  WinToast::instance()->setAppUserModelId(aumi);
  WinToast::instance()->initialize();
#endif

  try {
    _wbi_config = BilibiliDynamic::FetchWbiConfig(
        DataManager::GetInstance()->GetWithDefault("cookies", ""),
        DataManager::GetInstance()->GetWithDefault("user-agent", ""));
  } catch (const std::exception& e) {
    _wbi_config.reset();
    LAppPal::PrintLog(LogLevel::Warn,
                      "[UserStateManager]Fetch WBI key failed %s", e.what());
  }

  // init cookie window
#ifdef __APPLE__
  _cookieWindow = new CookieWindow(parent);
#else
  _cookieWindow = new CookieWindow(parent, GetModuleHandle(nullptr));
#endif
  // running check thread
  _checkThread = std::thread(&UserStateManager::CheckThread, this, list);
}

void UserStateManager::CheckThread(const vector<string>& list) {
  // sleep for 3 seconds to wait for cookie window
  if (Wait(3)) return;
#ifdef __APPLE__
  auto userAgent = _cookieWindow->GetUserAgent();
  if (!userAgent.empty())
    DataManager::GetInstance()->SetRaw("user-agent", userAgent);
#endif
  _mutex.lock();
  for (auto uid : list) {
    std::shared_ptr<UserStateWatcher> watcher =
      std::make_shared<UserStateWatcher>(uid,
          _cookieWindow->GetUserAgent(), _wbi_config);
    _watchers.push_back(watcher);
  }
  _mutex.unlock();
  int check_delay = 3;
  while (_running) {
#ifdef __APPLE__
    auto latestUserAgent = _cookieWindow->GetUserAgent();
    if (!latestUserAgent.empty() && latestUserAgent != userAgent) {
      userAgent = latestUserAgent;
      DataManager::GetInstance()->SetRaw("user-agent", userAgent);
    }
#endif
    // copy a shadow of _watchers
    _mutex.lock();
    std::vector<std::shared_ptr<UserStateWatcher>> watchers = _watchers;
    _mutex.unlock();
    for (auto watcher : watchers) {
      if (!_running) return;
      
      CheckStatus status = watcher->Check(_messageQueue, FetchCookies());
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
          if (wdesc.empty()) {
            wdesc = L"动态有更新";
          }
          Notify(wuname + L" - 新动态", wdesc,
                 new WinToastEventHandler("https://t.bilibili.com/" +
                                          messageInfo.extra1));
        }
      }
      if (status != CheckStatus::SUCCESS) {
        check_delay *= 2;
        LAppPal::PrintLog(LogLevel::Warn, "[UserStateManager]API failure make delay updated to %d", check_delay);
      } else {
        if (check_delay >= 12) {
          check_delay /= 3;
        } 
      }
      if (check_delay >= 60) {
#ifdef __APPLE__
        Platform::Alert(L"Error", L"获取直播信息失败，请在出现的窗口中点击完成可能出现的验证码，随后关闭窗口");
#else
        MessageBox(nullptr, L"获取直播信息失败，请在出现的窗口中点击完成可能出现的验证码，随后关闭窗口",
                   L"Error", MB_OK);
#endif
        _cookieWindow->Show();
        goto skip;
      }
      if (Wait(check_delay)) return;
    }
  skip:
    // sleep for 10 seconds
    if (Wait(check_delay)) return;
  }
}
