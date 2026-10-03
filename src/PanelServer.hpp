#pragma once
#include <httplib.h>
#include <mutex>
#include <deque>
#include <cstdint>
#include <nlohmann/json.hpp>
#include "AccountAvatarCache.hpp"

class PanelServer {
 private:
  httplib::Server* server;
  std::mutex _mtx;
  std::deque<std::pair<uint64_t, std::string>> _messages;
  std::condition_variable _cv;
  uint64_t _messageId = 0;
  std::thread worker_;
  std::atomic_bool _stopping{false};
  AccountAvatarCache avatarCache_;


  PanelServer() { server = new httplib::Server(); };

  void initSSE();

  bool DataSinkHandle(httplib::DataSink& sink, uint64_t& cursor);

  void doServe();

  nlohmann::json getTaskStatus();

 public:
  static PanelServer* GetInstance() {
    static PanelServer* instance = new PanelServer();
    return instance;
  }

  ~PanelServer() {
    Stop();
    delete server;
  }

  void Start();
  void Stop();

  void Notify(const std::string& message);
};
