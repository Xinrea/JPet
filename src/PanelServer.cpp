#include "PanelServer.hpp"

void PanelServer::Start() {
  // start thread
  std::thread t(&PanelServer::doServe, this);
  t.detach();
}

void PanelServer::doServe() {
  server->Get("/", [](const httplib::Request& req, httplib::Response& res) {
    res.set_content("Hello World!", "text/plain");
  });
  server->listen("localhost", 8053);
}