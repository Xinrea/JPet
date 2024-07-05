#pragma once
#include <httplib.h>

class PanelServer {
 private:
  httplib::Server* server;
  PanelServer() { server = new httplib::Server(); };
  void doServe();

 public:
  static PanelServer* GetInstance() {
    static PanelServer instance;
    return &instance;
  }
  void Start();
  ~PanelServer() { delete server; }
};
