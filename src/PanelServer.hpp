#pragma once
#include <httplib.h>
#include <mutex>
#include <nlohmann/json.hpp>

class PanelServer {
 private:
  httplib::Server* server;
  std::mutex _mtx;
  std::string _message;
  std::condition_variable _cv;
  std::atomic_int _messageId = 0;
  std::thread worker_;
  std::atomic_bool _stopping{false};


  PanelServer() { server = new httplib::Server(); };

  void initSSE();

  bool DataSinkHandle(httplib::DataSink& sink);

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
