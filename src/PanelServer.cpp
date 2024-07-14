#include "PanelServer.hpp"
#include "LAppPal.hpp"
#include "LAppDelegate.hpp"

#include <nlohmann/json.hpp>

void PanelServer::Start() {
  // start thread
  std::thread t(&PanelServer::doServe, this);
  t.detach();
}

void PanelServer::doServe() {
  // server->set_base_dir("resources/panel/dist");
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
  server->Get("/(.*)", [](const httplib::Request& req, httplib::Response& res) {
    httplib::Client cli("localhost", 5173);
    auto result = cli.Get(req.path);
    // result content type
    auto content_type = result->get_header_value("Content-Type");
    if (result) {
      res.set_content(result->body, content_type);
    } else {
      res.status = 500;
    }
  });
  server->listen("localhost", 8053);
}