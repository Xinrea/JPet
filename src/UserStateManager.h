#include <queue>
#include <thread>
#include <optional>
#include <mutex>

#include "StateMessage.hpp"
#include "UserStateWatcher.h"
#include "CookieWindow.hpp"
using std::queue;

class UserStateManager {
 public:
  ~UserStateManager() {
    _mutex.lock();
    for (auto watcher : _watchers) {
      delete watcher;
    }
    _watchers.clear();
    _mutex.unlock();
    _running = false;
  }
  void Init(const std::vector<std::string>& list, HWND parent) {
    // init cookie window
    _cookieWindow = new CookieWindow(parent, GetModuleHandle(nullptr));
    // running check thread
    _checkThread = std::thread(&UserStateManager::CheckThread, this, list);
    _checkThread.detach();
  }

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

  bool CheckUpdate();

  void CheckThread(const vector<string>& list) {
    // sleep for 3 seconds to wait for cookie window
    std::this_thread::sleep_for(std::chrono::seconds(3));
    _mutex.lock();
    for (auto uid : list) {
      UserStateWatcher* watcher = new UserStateWatcher(
          uid, _cookieWindow->cookie, _cookieWindow->userAgent);
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
      LAppPal::PrintLog(
          "[UserStateManager]CheckThread running with %d watchers",
          watchers.size());
      for (auto watcher : watchers) {
        _messageMutex.lock();
        bool cookieValid = watcher->Check(_messageQueue);
        _messageMutex.unlock();
        // sleep for 3 second
        std::this_thread::sleep_for(std::chrono::seconds(3));
        if (!cookieValid) {
          MessageBox(nullptr,
                     L"获取动态信息失败，请在出现的窗口中点击完成验证码",
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

  std::optional<StateMessage> FetchOne() {
    std::lock_guard<std::mutex> lock(_messageMutex);
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
  mutable std::mutex _messageMutex;

  CookieWindow* _cookieWindow = nullptr;

  std::thread _checkThread;
  bool _running = true;
};
