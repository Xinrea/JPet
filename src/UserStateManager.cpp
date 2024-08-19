#include "UserStateManager.h"
#include "LAppDefine.hpp"
#include "LAppPal.hpp"
#include "Wbi.hpp"

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif

#include <httplib.h>
#include <semver.hpp>

using namespace WinToastLib;

void UserStateManager::CheckUpdate() {
  httplib::SSLClient live_cli("pet.vjoi.cn", 443);
  live_cli.set_follow_location(true);
  live_cli.enable_server_certificate_verification(false);
  live_cli.set_connection_timeout(std::chrono::seconds(1));
  auto res = live_cli.Get("/version.txt");
  if (res && res->status == 200) {
    LAppPal::PrintLog(
        LogLevel::Debug,
        (std::string("[UserStateWatcher]Check Update Latest: ") + res->body)
            .c_str());
    bool need_check = true;
    string version_str = string(res->body);
    size_t endpos = version_str.find_last_not_of('\n');
    if (endpos != std::string::npos) {
      version_str = version_str.substr(0, endpos + 1);
    }
    try {
      semver::version latest_version{version_str};
    } catch (const std::exception& e) {
      LAppPal::PrintLog(LogLevel::Warn, "[UserStateManager]Latest version is not formatted %s", e.what());
      need_check = false;
    }
    if (!need_check) {
      return;
    }
    semver::version latest_version{version_str};
    semver::version local_version{(VERSION)};
    
    if (local_version < latest_version) {
      LAppPal::PrintLog("[UserStateWatcher]Need Update: %s -> %s ", VERSION,
                        res->body.c_str());
      Notify(L"检测到新版本", L"请前往项目页面下载",
             new WinToastEventHandler("https://pet.vjoi.cn"));
    }
  } else {
    LAppPal::PrintLog(LogLevel::Error, "[UserStateWatcher]Check Update Failed");
  }
}

void UserStateManager::Notify(const wstring& title, const wstring& content,
                              WinToastEventHandler* handler) {
  WinToastTemplate templ = WinToastTemplate(WinToastTemplate::ImageAndText02);
  // convert char* to wstring
  templ.setTextField(title, WinToastTemplate::FirstLine);
  templ.setTextField(content, WinToastTemplate::SecondLine);
  std::wstring img = LAppDefine::execPath + std::wstring(L"resources/imgs/Avatar.png");
  templ.setImagePath(img);
  WinToast::instance()->showToast(templ, handler, nullptr);
}

void UserStateManager::Init(const std::vector<std::string>& list, HWND parent) {
  // 通知初始化
  LAppPal::PrintLog(LogLevel::Info, "[LAppDelegate]Notification Init");
  WinToast::instance()->setAppName(L"JPet");
  const auto aumi =
      WinToast::configureAUMI(L"JoiGroup", L"JPetProject", L"JPet", LAppPal::StringToWString(VERSION));
  WinToast::instance()->setAppUserModelId(aumi);
  WinToast::instance()->initialize();

  auto p = Wbi::Get_wbi_key();
  img_key = p.first;
  sub_key = p.second;

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
    std::shared_ptr<UserStateWatcher> watcher =
      std::make_shared<UserStateWatcher>(uid,
          _cookieWindow->userAgent, img_key, sub_key);
    _watchers.push_back(watcher);
  }
  _mutex.unlock();
  int check_delay = 3;
  while (_running) {
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
          Notify(wuname + L" - 新动态", wdesc,
                 new WinToastEventHandler("https://t.bilibili.com/" +
                                          messageInfo.extra1));
        }
      }
      if (status != CheckStatus::SUCCESS) {
        check_delay *= 2;
        LAppPal::PrintLog(LogLevel::Warn, "[UserStateManager]API failure make delay updated to %d", check_delay);
        if (status == CheckStatus::RESTRICT) {
          _cookieWindow->UpdateCookie();
        }
      } else {
        if (check_delay >= 12) {
          check_delay /= 3;
        } 
      }
      if (check_delay >= 60) {
        MessageBox(nullptr, L"获取动态信息失败，请在出现的窗口中点击完成可能出现的验证码，随后关闭窗口",
                   L"Error", MB_OK);
        _cookieWindow->Show();
        goto skip;
      }
      std::this_thread::sleep_for(std::chrono::seconds(check_delay));
    }
  skip:
    // sleep for 10 seconds
    std::this_thread::sleep_for(std::chrono::seconds(check_delay));
  }
}
