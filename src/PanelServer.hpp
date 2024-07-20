#pragma once
#include <httplib.h>
#include <mutex>

class PanelServer {
 private:
  httplib::Server* server;
  std::mutex _mtx;
  std::string _message;
  std::condition_variable _cv;
  std::atomic_int _messageId = 0;


  PanelServer() { server = new httplib::Server(); };

  void initSSE();

  void DataSinkHandle(httplib::DataSink& sink);

  void doServe();

 public:
  static PanelServer* GetInstance() {
    static PanelServer instance;
    return &instance;
  }

  void Start();

  void Notify(const std::string& message);

  ~PanelServer() { delete server; }
};
