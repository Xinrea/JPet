#include "PanelServer.hpp"
#include "LAppPal.hpp"
#include "LAppDelegate.hpp"
#include "DataManager.hpp"

#include <nlohmann/json.hpp>

void PanelServer::Start() {
  // start thread
  std::thread t(&PanelServer::doServe, this);
  t.detach();
}

void PanelServer::DataSinkHandle(httplib::DataSink& sink) {
  std::unique_lock<std::mutex> lock(_mtx);
  int id = _messageId + 1;
  _cv.wait(lock, [&] { return _messageId == id; });
  sink.write(_message.c_str(), _message.size());
}

void PanelServer::Notify(const std::string& message) {
  std::lock_guard<std::mutex> lock(_mtx);
  _message = "data: " + message + "\n\n";
  _messageId++;
  _cv.notify_all();
}

void PanelServer::initSSE() {
  // client send a request and wait for response
  // if any message is sent to the client, the client will wait for response
  // again so we get a connection to notify the client
  server->Get("/api/sse", [this](const httplib::Request& req,
                                 httplib::Response& res) {
    LAppPal::PrintLog(LogLevel::Debug, "GET /api/sse");
    res.set_chunked_content_provider(
        "text/event-stream", [&](size_t /*offset*/, httplib::DataSink& sink) {
          // this will block until server wants to send message
          DataSinkHandle(sink);
          return true;
        });
  });
}

void PanelServer::doServe() {
  server->set_base_dir("resources/panel/dist");
  server->Get("/api/profile",
              [](const httplib::Request& req, httplib::Response& res) {
                LAppPal::PrintLog(LogLevel::Debug, "GET /api/profile");
                auto json = nlohmann::json::object();
                json["exp"] = DataManager::GetInstance()->GetExp();
                res.set_content(json.dump(), "application/json");
              });
  server->Get("/api/config/audio",
              [](const httplib::Request& req, httplib::Response& res) {
                int volume = LAppDelegate::GetInstance()->GetVolume();
                bool mute = LAppDelegate::GetInstance()->GetMute();
                auto json = nlohmann::json::object();
                json["volume"] = volume;
                json["mute"] = mute;
                res.set_content(json.dump(), "application/json");
              });
  server->Post("/api/config/audio",
               [](const httplib::Request& req, httplib::Response& res) {
                 LAppPal::PrintLog("POST /api/config/audio");
                 try {
                   auto json = nlohmann::json::parse(req.body);
                   LAppDelegate::GetInstance()->SetMute(json.at("mute"));
                   LAppDelegate::GetInstance()->SetVolume(json.at("volume"));
                   LAppDelegate::GetInstance()->SaveSettings();
                   res.status = 200;
                 } catch (nlohmann::json::exception& e) {
                   LAppPal::PrintLog("json parse error: %s", e.what());
                   res.status = 400;
                 }
               });
  server->Get("/api/config/display", [](const httplib::Request& req,
                                        httplib::Response& res) {
    bool green = LAppDelegate::GetInstance()->Green;
    bool limit = LAppDelegate::GetInstance()->isLimit;
    nlohmann::json resp = {{"green", green},
                           {"limit", limit},
                           {"scale", LAppDelegate::GetInstance()->GetScale()}};
    res.set_content(resp.dump(), "application/json");
  });
  server->Post("/api/config/display",
               [](const httplib::Request& req, httplib::Response& res) {
                 LAppPal::PrintLog("POST /api/config/display");
                 try {
                   auto json = nlohmann::json::parse(req.body);
                   LAppDelegate::GetInstance()->SetGreen(json.at("green"));
                   LAppDelegate::GetInstance()->SetLimit(json.at("limit"));
                   LAppDelegate::GetInstance()->SetScale(json.at("scale"));
                   LAppDelegate::GetInstance()->SaveSettings();
                   res.status = 200;
                 } catch (nlohmann::json::exception& e) {
                   LAppPal::PrintLog("json parse error: %s", e.what());
                   res.status = 400;
                 }
               });
  server->Get("/api/config/notify", [](const httplib::Request& req,
                                       httplib::Response& res) {
    bool dynamic = LAppDelegate::GetInstance()->DynamicNotify;
    bool live = LAppDelegate::GetInstance()->LiveNotify;
    bool update = LAppDelegate::GetInstance()->UpdateNotify;
    auto userStateWatcher = LAppDelegate::GetInstance()->GetUserStateManager();
    vector<WatchTarget> followList;
    if (userStateWatcher) {
      userStateWatcher->GetTargetList(followList);
    }
    nlohmann::json followListJson;
    for (auto target : followList) {
      followListJson.push_back({
          {"uid", target.uid},
          {"uname", target.uname},
      });
    }
    nlohmann::json resp = {{"dynamic", dynamic},
                           {"live", live},
                           {"update", update},
                           {"watch_list", followListJson}};
    res.set_content(resp.dump(), "application/json");
  });
  server->Post("/api/config/notify", [](const httplib::Request& req,
                                        httplib::Response& res) {
    nlohmann::json json = nlohmann::json::parse(req.body);
    LAppDelegate::GetInstance()->LiveNotify = json.at("live");
    LAppDelegate::GetInstance()->DynamicNotify = json.at("dynamic");
    LAppDelegate::GetInstance()->UpdateNotify = json.at("update");
    LAppDelegate::GetInstance()->SaveSettings();
  });
  server->Put("/api/config/notify", [](const httplib::Request& req,
                                       httplib::Response& res) {
    nlohmann::json json = nlohmann::json::parse(req.body);
    std::string uid = json.at("uid");
    if (uid == "") {
      LAppPal::PrintLog("[PUT /api/config/notify]UID is empty");
      res.status = 400;
    } else {
      LAppDelegate::GetInstance()->GetUserStateManager()->AddWatcher(uid);
      LAppDelegate::GetInstance()->SaveSettings();
    }
    // response with updated follow list
    vector<WatchTarget> followList;
    LAppDelegate::GetInstance()->GetUserStateManager()->GetTargetList(
        followList);
    nlohmann::json followListJson;
    for (auto target : followList) {
      followListJson.push_back({
          {"uid", target.uid},
          {"uname", target.uname},
      });
    }
    nlohmann::json resp = {{"watch_list", followListJson}};
    res.set_content(resp.dump(), "application/json");
  });
  server->Delete("/api/config/notify", [](const httplib::Request& req,
                                          httplib::Response& res) {
    auto json = nlohmann::json::parse(req.body);
    std::string uid = json.at("uid");
    LAppDelegate::GetInstance()->GetUserStateManager()->RemoveWatcher(uid);
    LAppDelegate::GetInstance()->SaveSettings();
    // response with updated follow list
    vector<WatchTarget> followList;
    LAppDelegate::GetInstance()->GetUserStateManager()->GetTargetList(
        followList);
    nlohmann::json followListJson;
    for (auto target : followList) {
      followListJson.push_back({
          {"uid", target.uid},
          {"uname", target.uname},
      });
    }
    nlohmann::json resp = {{"watch_list", followListJson}};
    res.set_content(resp.dump(), "application/json");
  });

  initSSE();

  server->listen("localhost", 8053);
}
