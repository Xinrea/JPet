#include "PanelServer.hpp"

void PanelServer::Start() {
  // start thread
  std::thread t(&PanelServer::doServe, this);
  t.detach();
}

void PanelServer::doServe() {
  server->Get("/", [](const httplib::Request& req, httplib::Response& res) {
    // send response panel/index.html
    std::ifstream ifs("resources/panel/index.html");
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        (std::istreambuf_iterator<char>()));
    res.set_content(content, "text/html");
  });
  server->listen("localhost", 8053);
}