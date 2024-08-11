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


  PanelServer() { server = new httplib::Server(); };

  void initSSE();

  void DataSinkHandle(httplib::DataSink& sink);

  void doServe();

  nlohmann::json getTaskStatus();

 public:
  static PanelServer* GetInstance() {
    static PanelServer* instance = new PanelServer();
    return instance;
  }

  ~PanelServer() {
    server->stop();
    delete server;
  }

  void Start();

  void Notify(const std::string& message);
};
