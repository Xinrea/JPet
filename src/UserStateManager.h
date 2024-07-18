#pragma once

#include <queue>
#include <thread>
#include <optional>
#include <mutex>

#include "StateMessage.hpp"
#include "UserStateWatcher.h"
#include "CookieWindow.hpp"
#include "LAppDefine.hpp"
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
    _mutex.lock();
    for (auto watcher : _watchers) {
      delete watcher;
    }
    _watchers.clear();
    _mutex.unlock();
    _running = false;
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
    UserStateWatcher* watcher = new UserStateWatcher(uid, _cookieWindow->cookie,
                                                     _cookieWindow->userAgent);
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

  void GetTargetList(std::vector<WatchTarget>& list) {
    std::lock_guard<std::mutex> lock(_mutex);
    for (auto watcher : _watchers) {
      list.push_back(watcher->target);
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

 private:
  vector<UserStateWatcher*> _watchers;
  mutable std::mutex _mutex;
  queue<StateMessage> _messageQueue;
  wstring _exePath;
  const bool& _dynamicNotifyEnabled;
  const bool& _liveNotifyEnabled;

  CookieWindow* _cookieWindow = nullptr;

  std::thread _checkThread;
  bool _running = true;

  void Notify(const wstring& title, const wstring& content,
              WinToastEventHandler* handler);
};
