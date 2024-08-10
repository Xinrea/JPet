#pragma once

#include <queue>
#include <thread>
#include <optional>
#include <map>
#include <mutex>

#include "StateMessage.hpp"
#include "UserStateWatcher.h"
#include "CookieWindow.hpp"
#include "LAppDefine.hpp"
#include "DataManager.hpp"
#include "wintoastlib.h"
#include "WinToastEventHandler.h"

using std::queue;

class UserStateManager {
 public:
  UserStateManager(const bool& dynamicNotifyEnabled,
                   const bool& liveNotifyEnabled)
      : _dynamicNotifyEnabled(dynamicNotifyEnabled),
        _liveNotifyEnabled(liveNotifyEnabled) {}
  ~UserStateManager() {
    _running = false;
    _mutex.lock();
    _watchers.clear();
    _mutex.unlock();
  }
  void Init(const std::vector<std::string>& list, HWND parent);

  void AddWatcher(const std::string& uid) {
    std::lock_guard<std::mutex> lock(_mutex);
    // check if already exist
    for (auto watcher : _watchers) {
      if (watcher->target.uid == uid) {
        return;
      }
    }
    std::shared_ptr<UserStateWatcher> watcher = std::make_shared<UserStateWatcher>(uid,
        _cookieWindow->userAgent, img_key, sub_key);
    _watchers.push_back(watcher);
    LAppPal::PrintLog("[UserStateManager]Add watcher %s", uid.c_str());
  }

  void RemoveWatcher(const std::string& uid) {
    std::lock_guard<std::mutex> lock(_mutex);
    for (auto it = _watchers.begin(); it != _watchers.end(); it++) {
      if ((*it)->target.uid == uid) {
        _watchers.erase(it);
        break;
      }
    }
    LAppPal::PrintLog("[UserStateManager]Remove watcher %s", uid.c_str());
  }

  void GetTargetList(std::map<string, WatchTarget>& mtarget) {
    std::lock_guard<std::mutex> lock(_mutex);
    for (auto watcher : _watchers) {
      auto target = watcher->target;
      mtarget.insert(std::pair<string, WatchTarget>(target.uid, target));
    }
  }

  void CheckUpdate();

  void CheckThread(const vector<string>& list);

  std::optional<StateMessage> FetchOne() {
    if (!_messageQueue.empty()) {
      StateMessage message = _messageQueue.front();
      _messageQueue.pop();
      return message;
    }
    return std::nullopt;
  }

  string FetchCookies() {
    auto cookies = DataManager::GetInstance()->GetWithDefault("cookies", "");
    if (cookies.empty()) {
      cookies = _cookieWindow->cookie;
    }
    return cookies;
  }

  std::pair<string, string> GetWbiKey() {
    return std::pair<string, string>(img_key, sub_key);
  }

 private:
  vector<std::shared_ptr<UserStateWatcher>> _watchers;
  std::mutex _mutex;
  queue<StateMessage> _messageQueue;
  wstring _exePath;
  const bool& _dynamicNotifyEnabled;
  const bool& _liveNotifyEnabled;

  string img_key, sub_key;

  CookieWindow* _cookieWindow = nullptr;

  std::thread _checkThread;
  bool _running = true;

  void Notify(const wstring& title, const wstring& content,
              WinToastEventHandler* handler);
};
