#include "PanelServer.hpp"
#include "BilibiliLogin.hpp"
#include "BuffManager.hpp"
#include "GameManager.hpp"
#include "DataManager.hpp"
#include "GameTask.hpp"
#include "LAppDefine.hpp"
#include "LAppPal.hpp"
#include "PartStateManager.h"
#include "LAppDelegate.hpp"
#include "Wbi.hpp"
#include "Platform.hpp"
#include "UpdateManager.hpp"
#include "VoiceChat.hpp"
#include "VoicePlatform.hpp"

#include <cstdlib>
#include <map>
#include <string_view>

namespace {

// Use the same request identity for QR generation, polling and ticket exchange.
httplib::Headers LoginHeaders() {
  return {{"User-Agent", BilibiliLogin::UserAgent},
          {"Referer", "https://passport.bilibili.com/"}};
}

BilibiliLogin::Response LoginResponse(const httplib::Response& response) {
  BilibiliLogin::Response result;
  result.status = response.status;
  result.location = response.get_header_value("Location");
  const auto count = response.get_header_value_count("Set-Cookie");
  for (size_t i = 0; i < count; ++i) {
    result.setCookies.push_back(response.get_header_value("Set-Cookie", i));
  }
  return result;
}

BilibiliLogin::Result ResolveQrLoginCookies(const httplib::Response& poll,
                                           const std::string& loginUrl) {
  return BilibiliLogin::Resolve(LoginResponse(poll), loginUrl,
      [](const std::string& host, const std::string& path,
         const std::string& cookies) -> std::optional<BilibiliLogin::Response> {
        httplib::SSLClient client(host, 443);
        client.set_follow_location(false);
        client.set_connection_timeout(std::chrono::seconds(3));
        client.set_read_timeout(std::chrono::seconds(5));
        auto headers = LoginHeaders();
        if (!cookies.empty()) headers.emplace("Cookie", cookies);
        auto response = client.Get(path, headers);
        if (!response) return std::nullopt;
        return LoginResponse(*response);
      });
}

std::string CookieValue(const std::string& cookies, std::string_view wanted) {
  size_t start = 0;
  while (start < cookies.size()) {
    while (start < cookies.size() &&
           (cookies[start] == ';' || cookies[start] == ' ' ||
            cookies[start] == '\t')) {
      ++start;
    }
    const auto end = cookies.find(';', start);
    const auto pair = cookies.substr(start, end == std::string::npos
                                             ? std::string::npos
                                             : end - start);
    const auto equals = pair.find('=');
    if (equals != std::string::npos && pair.substr(0, equals) == wanted) {
      return pair.substr(equals + 1);
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return {};
}

// Voice settings plus the connected microphones and speakers to choose from.
nlohmann::json VoiceSettingsWithDevices() {
  auto settings = DataManager::GetInstance()->GetVoiceSettings();
  const auto devices = Voice::ListAudioDevices();
  const auto list = [](const std::vector<Voice::AudioDevice>& items) {
    auto out = nlohmann::json::array();
    for (const auto& device : items) out.push_back({{"id", device.id}, {"name", device.name}});
    return out;
  };
  settings["audio_devices"] = {{"inputs", list(devices.inputs)}, {"outputs", list(devices.outputs)}};
  return settings;
}

}  // namespace

void PanelServer::Start() {
  worker_ = std::thread(&PanelServer::doServe, this);
}

void PanelServer::Stop() {
  _stopping = true;
  server->stop();
  _cv.notify_all();
  if (worker_.joinable()) worker_.join();
}

bool PanelServer::DataSinkHandle(httplib::DataSink &sink, uint64_t& cursor) {
  std::unique_lock<std::mutex> lock(_mtx);
  _cv.wait_for(lock, std::chrono::seconds(10), [&] { return _stopping || _messageId != cursor; });
  if (_stopping) return false;
  std::string message;
  for (const auto& [id, event] : _messages) if (id > cursor) message += event;
  cursor = _messageId;
  if (message.empty()) message = ": keepalive\n\n";
  lock.unlock();
  return sink.write(message.c_str(), message.size());
}

void PanelServer::Notify(const std::string &message) {
  std::lock_guard<std::mutex> lock(_mtx);
  // A task can unlock several achievements, then immediately send UPDATE.
  // Retain recent events so the refresh cannot overwrite the unlock notice.
  _messages.emplace_back(++_messageId, "data: " + message + "\n\n");
  if (_messages.size() > 64) _messages.pop_front();
  _cv.notify_all();
}

void PanelServer::initSSE() {
  // client send a request and wait for response
  // if any message is sent to the client, the client will wait for response
  // again so we get a connection to notify the client
  server->Get("/api/sse", [this](const httplib::Request &req,
                                 httplib::Response &res) {
    LAppPal::PrintLog(LogLevel::Debug, "GET /api/sse");
    uint64_t cursor;
    {
      std::lock_guard<std::mutex> lock(_mtx);
      cursor = _messageId;
    }
    res.set_chunked_content_provider(
        "text/event-stream", [this, cursor](size_t /*offset*/, httplib::DataSink &sink) mutable {
          // this will block until server wants to send message
          return DataSinkHandle(sink, cursor);
        });
  });
}

nlohmann::json PanelServer::getTaskStatus() {
  return GameManager::GetInstance()->Snapshot()["tasks"];
}

void PanelServer::doServe() {
  int port = 8053;
  // A smoke test can run beside the user's app with an isolated profile/port.
  if (std::getenv("JPET_SMOKE_TEST")) {
    if (const auto* testPort = std::getenv("JPET_SMOKE_PORT")) {
      char* end = nullptr;
      const auto parsed = std::strtol(testPort, &end, 10);
      if (*testPort && !*end && parsed > 0 && parsed <= 65535) port = static_cast<int>(parsed);
    }
  }
  const auto localRequest = [port](const httplib::Request& req, httplib::Response& res, bool post, size_t limit = 2048) {
    const auto authority = "127.0.0.1:" + std::to_string(port), localhost = "localhost:" + std::to_string(port);
    const auto host = req.get_header_value("Host"), origin = req.get_header_value("Origin");
    if ((host != authority && host != localhost) || (!origin.empty() && origin != "http://" + authority && origin != "http://" + localhost) ||
        (post && (req.get_header_value("Content-Type").find("application/json") != 0 || req.body.size() > limit))) {
      res.status = 403; res.set_content(R"({"error":"请求来源无效"})", "application/json"); return false;
    }
    res.set_header("Cache-Control", "no-store"); return true;
  };
  server->set_base_dir("resources/panel/dist");
  server->Post("/api/log", [](const httplib::Request &req,
                              httplib::Response &res) {
    if (req.body.size() > 4096) {
      res.status = 413;
      return;
    }
    try {
      const auto json = nlohmann::json::parse(req.body);
      const auto message = json.value("message", std::string{});
      if (message.empty()) {
        res.status = 400;
        return;
      }
      const auto level = json.value("level", std::string{"error"});
      if (level == "debug") {
        LAppPal::PrintLog(LogLevel::Debug, "[WebView] %s", message.c_str());
      } else if (level == "info") {
        LAppPal::PrintLog(LogLevel::Info, "[WebView] %s", message.c_str());
      } else if (level == "warn") {
        LAppPal::PrintLog(LogLevel::Warn, "[WebView] %s", message.c_str());
      } else {
        LAppPal::PrintLog(LogLevel::Error, "[WebView] %s", message.c_str());
      }
      res.status = 204;
    } catch (const std::exception &e) {
      LAppPal::PrintLog(LogLevel::Warn, "[WebView]Invalid log payload: %s",
                        e.what());
      res.status = 400;
    }
  });
  auto gameAction = [](httplib::Response& res, const nlohmann::json& action) {
    auto* game = GameManager::GetInstance();
    auto error = game->Command(action);
    res.status = error.empty() ? 200 : 409;
    res.set_content(error.empty() ? game->Snapshot()["profile"].dump()
        : nlohmann::json{{"error", error}}.dump(), "application/json");
  };
  server->Post("/api/star", [gameAction](const auto&, auto& res) { gameAction(res, {{"type", "star"}}); });
  server->Post("/api/attr/:attr", [gameAction](const auto& req, auto& res) { gameAction(res, {{"type", "attr.buy"}, {"attr", req.path_params.at("attr")}}); });
  server->Delete("/api/attr/:attr", [gameAction](const auto& req, auto& res) { gameAction(res, {{"type", "attr.refund"}, {"attr", req.path_params.at("attr")}}); });
  server->Get("/api/profile", [](const auto&, auto& res) {
    res.set_content(GameManager::GetInstance()->Snapshot()["profile"].dump(), "application/json");
  });
  server->Get("/api/achievements", [](const httplib::Request&, httplib::Response& res) {
    try {
      res.set_content(GameManager::GetInstance()->Snapshot()["achievements"].dump(), "application/json");
    } catch (const std::exception& e) {
      LAppPal::PrintLog(LogLevel::Error, "[Achievements]Load failed: %s", e.what());
      res.status = 500;
      res.set_content("{\"error\":\"成就加载失败，请重试\"}", "application/json");
    }
  });
  server->Get("/api/parts", [](const httplib::Request& req, httplib::Response& res){
    const map<string, bool> part_status = PartStateManager::GetInstance()->GetStatus();
    auto json = nlohmann::json::object();
    for (const auto& [key, status] : part_status) {
      json[key] = status;
    }
    res.set_content(json.dump(), "application/json");
  });
  server->Post("/api/parts",
               [](const httplib::Request &req, httplib::Response &res) {
                 auto json = nlohmann::json::parse(req.body);
                 LAppPal::PrintLog(LogLevel::Debug, "POST /api/parts [%s]%d",
                                   json["param"].get<string>().c_str(),
                                   json["enable"].get<bool>());
                 PartStateManager::GetInstance()->Toggle(
                     json["param"].get<string>(), json["enable"].get<bool>());
               });
  server->Post("/api/clothes/:id", [&](const httplib::Request &req,
                                      httplib::Response &res) {
    int id = std::stoi(req.path_params.at("id"));
    LAppPal::PrintLog(LogLevel::Debug, "POST /api/clothes/%d", id);
    if (id < 0 || id > 2) {
      LAppPal::PrintLog(LogLevel::Warn, "[PanelServer]Invalid clothes id");
      res.status = 400;
      return;
    }
    auto error = GameManager::GetInstance()->Command({{"type", "clothes"}, {"id", id}});
    res.status = error.empty() ? 200 : 409;
    res.set_content(error.empty() ? "{\"success\":true}" : nlohmann::json{{"error", error}}.dump(), "application/json");
  });
  server->Get("/api/task",
              [&](const httplib::Request &req, httplib::Response &res) {
                LAppPal::PrintLog(LogLevel::Debug, "GET /api/task");
                try {
                  auto data = getTaskStatus();
                  res.set_content(data.dump(), "application/json");
                } catch (const std::exception &e) {
                  res.status = 500;
                  res.set_content(e.what(), "text/plain");
                  LAppPal::PrintLog(LogLevel::Error, e.what());
                }
              });
  auto taskResponse = [this](httplib::Response& res,
                             const std::function<std::string()>& action) {
    try {
      auto error = action();
      if (!error.empty()) {
        res.status = 409;
        res.set_content(nlohmann::json{{"error", error}}.dump(), "application/json");
        return;
      }
      res.set_content(getTaskStatus().dump(), "application/json");
    } catch (const std::exception& e) {
      res.status = 400;
      res.set_content(nlohmann::json{{"error", "任务操作失败，请刷新后重试"}}.dump(), "application/json");
      LAppPal::PrintLog(LogLevel::Warn, "[Tasks]API request failed: %s", e.what());
    }
  };
  server->Post("/api/task/queue/upgrade", [taskResponse](const httplib::Request&,
                                                      httplib::Response& res) {
    taskResponse(res, [] { return GameManager::GetInstance()->Command({{"type", "queue.upgrade"}}); });
  });
  server->Post("/api/task/:id/start", [taskResponse](const httplib::Request& req,
                                                   httplib::Response& res) {
    taskResponse(res, [&] {
      return GameManager::GetInstance()->Command({{"type", "task.start"}, {"id", std::stoi(req.path_params.at("id"))}});
    });
  });
  server->Post("/api/task/:id/queue", [taskResponse](const httplib::Request& req,
                                                   httplib::Response& res) {
    taskResponse(res, [&] {
      return GameManager::GetInstance()->Command({{"type", "task.queue"}, {"id", std::stoi(req.path_params.at("id"))}});
    });
  });
  server->Delete("/api/task/queue/:entryId", [taskResponse](const httplib::Request& req,
                                                         httplib::Response& res) {
    taskResponse(res, [&] {
      return GameManager::GetInstance()->Command({{"type", "queue.remove"}, {"entry_id", std::stoll(req.path_params.at("entryId"))}});
    });
  });
  server->Post("/api/task/queue/:entryId/move", [taskResponse](const httplib::Request& req,
                                                           httplib::Response& res) {
    taskResponse(res, [&] {
      auto payload = nlohmann::json::parse(req.body);
      return GameManager::GetInstance()->Command({{"type", "queue.move"},
          {"entry_id", std::stoll(req.path_params.at("entryId"))}, {"direction", payload.at("direction").get<int>()}});
    });
  });
  server->Post("/api/task/:id/cancel", [taskResponse](const httplib::Request& req,
                                                    httplib::Response& res) {
    taskResponse(res, [&] {
      return GameManager::GetInstance()->Command({{"type", "task.cancel"}, {"id", std::stoi(req.path_params.at("id"))}});
    });
  });
  server->Post("/api/config/folder", [](const httplib::Request &req, httplib::Response &res) {
    Platform::Open(LAppPal::WStringToString(LAppDefine::documentPath));
  });
  server->Post("/api/openlink",
               [](const httplib::Request &req, httplib::Response &res) {
                 auto json = nlohmann::json::parse(req.body);
                 Platform::Open(json.at("link").get<std::string>());
               });
  server->Get("/api/config/audio",
              [](const httplib::Request &req, httplib::Response &res) {
                bool mute;
                int volume;
                bool idle_audio, touch_audio;
                DataManager::GetInstance()->GetAudio(&volume, &mute, &idle_audio, &touch_audio);
                auto json = nlohmann::json::object();
                json["volume"] = volume;
                json["mute"] = mute;
                json["idle_audio"] = idle_audio;
                json["touch_audio"] = touch_audio;
                res.set_content(json.dump(), "application/json");
              });
  server->Get("/api/config/voice", [](const httplib::Request&, httplib::Response& res) {
    res.set_header("Cache-Control", "no-store");
    res.set_content(VoiceSettingsWithDevices().dump(), "application/json");
  });
  server->Post("/api/config/voice", [localRequest](const httplib::Request& req, httplib::Response& res) {
    if (!localRequest(req, res, true, 4096)) return;
    res.set_header("Cache-Control", "no-store");
    if (req.body.size() > 4096 || req.get_header_value("Content-Type").find("application/json") != 0) {
      res.status = 400;
      res.set_content(R"({"error":"语音设置格式不正确"})", "application/json");
      return;
    }
    try {
      const auto payload = nlohmann::json::parse(req.body);
      std::string error;
      if (!DataManager::GetInstance()->UpdateVoiceSettings(payload, error)) {
        res.status = 400;
        res.set_content(nlohmann::json{{"error", error}}.dump(), "application/json");
        return;
      }
      VoiceChat::GetInstance()->ConfigurationChanged();
      res.set_content(VoiceSettingsWithDevices().dump(), "application/json");
    } catch (const nlohmann::json::exception&) {
      res.status = 400;
      res.set_content(R"({"error":"语音设置格式不正确"})", "application/json");
    }
  });
  server->Get("/api/voice", [](const httplib::Request&, httplib::Response& res) {
    res.set_header("Cache-Control", "no-store");
    res.set_content(VoiceChat::GetInstance()->Status().dump(), "application/json");
  });
  // POST because the request may carry an API key that is not saved yet.
  server->Post("/api/voice/chat-models", [localRequest](const httplib::Request& req, httplib::Response& res) {
    if (!localRequest(req, res, true, 4096)) return;
    const auto payload = nlohmann::json::parse(req.body, nullptr, false);
    std::string error;
    const auto models = payload.is_object() ? VoiceChat::GetInstance()->ChatModels(payload, error) : nlohmann::json();
    if (!models.is_object()) {
      res.status = 400;
      res.set_content(nlohmann::json{{"error", error.empty() ? "请求格式不正确" : error}}.dump(), "application/json");
      return;
    }
    res.set_content(models.dump(), "application/json");
  });
  server->Post("/api/voice/speech-model", [localRequest](const httplib::Request& req, httplib::Response& res) {
    if (!localRequest(req, res, true)) return;
    const auto payload = nlohmann::json::parse(req.body, nullptr, false);
    std::string error;
    if (!payload.is_object() || !payload.contains("action") || !payload["action"].is_string() ||
        !VoiceChat::GetInstance()->SpeechModelAction(payload["action"].get<std::string>(), error)) {
      res.status = 400;
      res.set_content(nlohmann::json{{"error", error.empty() ? "模型操作格式不正确" : error}}.dump(), "application/json");
      return;
    }
    res.set_content(VoiceChat::GetInstance()->SpeechModelStatus().dump(), "application/json");
  });
  server->Post("/api/voice/chat", [localRequest](const httplib::Request& req, httplib::Response& res) {
    if (!localRequest(req, res, true, 8192)) return;
    const auto payload = nlohmann::json::parse(req.body, nullptr, false);
    std::string error = "消息格式不正确";
    // `speak` (optional, default false) asks for the reply to be read aloud too.
    const bool speak = payload.is_object() && payload.contains("speak") && payload["speak"].is_boolean() &&
        payload["speak"].get<bool>();
    if (!payload.is_object() || !payload.contains("text") || !payload["text"].is_string() ||
        !VoiceChat::GetInstance()->SendText(payload["text"].get<std::string>(), speak, error)) {
      res.status = 400;
      res.set_content(nlohmann::json{{"error", error}}.dump(), "application/json");
      return;
    }
    res.set_content(R"({"success":true})", "application/json");
  });
  server->Post("/api/voice/chat/stop", [localRequest](const httplib::Request& req, httplib::Response& res) {
    if (!localRequest(req, res, false)) return;
    VoiceChat::GetInstance()->StopReply();
    res.set_content(R"({"success":true})", "application/json");
  });
  server->Get("/api/voice/history", [](const httplib::Request&, httplib::Response& res) {
    res.set_header("Cache-Control", "no-store");
    res.set_content(VoiceChat::GetInstance()->History().dump(), "application/json");
  });
  server->Delete("/api/voice/history", [localRequest](const httplib::Request& req, httplib::Response& res) {
    if (!localRequest(req, res, false)) return;
    if (!VoiceChat::GetInstance()->ClearHistory()) {
      res.status = 500;
      res.set_content(R"({"error":"对话记录已清空，但暂时无法保存，重启后可能恢复。"})", "application/json");
      return;
    }
    res.set_content(R"({"success":true})", "application/json");
  });
  server->Post("/api/config/audio",
               [](const httplib::Request &req, httplib::Response &res) {
                 LAppPal::PrintLog("POST /api/config/audio");
                 try {
                   auto json = nlohmann::json::parse(req.body);
                   DataManager::GetInstance()->UpdateAudio(
                       json.at("volume"), json.at("mute"),
                       json.at("idle_audio"), json.at("touch_audio"));
                   DataManager::GetInstance()->Save();
                   res.status = 200;
                 } catch (nlohmann::json::exception &e) {
                   LAppPal::PrintLog("json parse error: %s", e.what());
                   res.status = 400;
                 }
               });
  server->Get("/api/config/display", [](const httplib::Request &req,
                                        httplib::Response &res) {
    bool green = LAppDelegate::GetInstance()->Green;
    bool limit = LAppDelegate::GetInstance()->isLimit;
    nlohmann::json resp = {{"green", green},
                           {"limit", limit},
                           {"scale", LAppDelegate::GetInstance()->GetScale()}};
    res.set_content(resp.dump(), "application/json");
  });
  server->Post("/api/config/display",
               [](const httplib::Request &req, httplib::Response &res) {
                 LAppPal::PrintLog("POST /api/config/display");
                 try {
                   auto json = nlohmann::json::parse(req.body);
                   LAppDelegate::GetInstance()->SetGreen(json.at("green"));
                   LAppDelegate::GetInstance()->SetLimit(json.at("limit"));
                   LAppDelegate::GetInstance()->SetScale(json.at("scale"));
                   LAppDelegate::GetInstance()->SaveSettings();
                   res.status = 200;
                 } catch (nlohmann::json::exception &e) {
                   LAppPal::PrintLog("json parse error: %s", e.what());
                   res.status = 400;
                 }
               });
  server->Get("/api/config/other", [](const httplib::Request &req,
                                      httplib::Response &res) {
    nlohmann::json resp;
    resp["track"] =  DataManager::GetInstance()->IsTracking();
    resp["dropfile"] = DataManager::GetInstance()->GetDropFile();
    res.set_content(resp.dump(), "application/json");
  });
  server->Post("/api/config/other", [](const httplib::Request &req, httplib::Response &res) {
    auto json = nlohmann::json::parse(req.body);
    DataManager::GetInstance()->IsTracking(json.at("track"));
    DataManager::GetInstance()->UpdateDropFile(json.at("dropfile"));
    DataManager::GetInstance()->Save();
  });
  server->Get("/api/config/notify", [](const httplib::Request &req,
                                       httplib::Response &res) {
    bool dynamic, live, update;
    vector<string> followList = DataManager::GetInstance()->GetFollowList();
    DataManager::GetInstance()->GetNotify(&dynamic, &live, &update);
      
    map<string, WatchTarget> targetList;
    LAppDelegate::GetInstance()->GetUserStateManager()->GetTargetList(targetList);
    nlohmann::json followListJson;
    for (auto target : followList) {
      followListJson.push_back({
          {"uid", target},
          {"uname", targetList[target].uname},
      });
    }
    nlohmann::json resp = {{"dynamic", dynamic},
                           {"live", live},
                           {"update", update},
                           {"watch_list", followListJson}};
    res.set_content(resp.dump(), "application/json");
  });
  server->Post("/api/config/notify", [](const httplib::Request &req,
                                        httplib::Response &res) {
    nlohmann::json json = nlohmann::json::parse(req.body);
    DataManager::GetInstance()->UpdateNotify(json.at("dynamic"),
                                             json.at("live"),
                                             json.at("update"));
    LAppDelegate::GetInstance()->DynamicNotify = json.at("dynamic");
    LAppDelegate::GetInstance()->LiveNotify = json.at("live");
    LAppDelegate::GetInstance()->UpdateNotify = json.at("update");
  });
  server->Put("/api/config/notify", [](const httplib::Request &req,
                                       httplib::Response &res) {
    nlohmann::json json = nlohmann::json::parse(req.body);
    std::string uid = json.at("uid");
    if (uid == "") {
      LAppPal::PrintLog("[PUT /api/config/notify]UID is empty");
      res.status = 400;
    } else {
      LAppDelegate::GetInstance()->GetUserStateManager()->AddWatcher(uid);
      DataManager::GetInstance()->AddFollow(uid);
    }
    // response with updated follow list
    map<string, WatchTarget> followList;
    LAppDelegate::GetInstance()->GetUserStateManager()->GetTargetList(
        followList);
    nlohmann::json followListJson;
    for (auto target : followList) {
      followListJson.push_back({
          {"uid", target.second.uid},
          {"uname", target.second.uname},
      });
    }
    nlohmann::json resp = {{"watch_list", followListJson}};
    res.set_content(resp.dump(), "application/json");
  });
  server->Delete("/api/config/notify", [](const httplib::Request &req,
                                          httplib::Response &res) {
    auto json = nlohmann::json::parse(req.body);
    std::string uid = json.at("uid");
    LAppDelegate::GetInstance()->GetUserStateManager()->RemoveWatcher(uid);
    DataManager::GetInstance()->RemoveFollow(uid);
    // response with updated follow list
    map<string, WatchTarget> followList;
    LAppDelegate::GetInstance()->GetUserStateManager()->GetTargetList(
        followList);
    nlohmann::json followListJson;
    for (auto target : followList) {
      followListJson.push_back({
          {"uid", target.second.uid},
          {"uname", target.second.uname},
      });
    }
    nlohmann::json resp = {{"watch_list", followListJson}};
    res.set_content(resp.dump(), "application/json");
  });
  server->Get("/api/config/shortcut", [](const httplib::Request &req,
                                          httplib::Response &res) {
    nlohmann::json shortcuts;
    auto dm = DataManager::GetInstance();
    // contain 4 items
    for (int i = 0; i < 4; i++) {
      shortcuts.push_back(
          {{"type",
            dm->GetWithDefault("shortcut." + std::to_string(i) + ".type", 3)},
           {"param", dm->GetWithDefault(
                         "shortcut." + std::to_string(i) + ".param", "")}});
    }
    res.set_content(shortcuts.dump(), "application/json");
  });
  server->Post("/api/config/shortcut/:id",
               [](const httplib::Request &req, httplib::Response &res) {
                 string id = req.path_params.at("id");
                 auto json = nlohmann::json::parse(req.body);
                 auto dm = DataManager::GetInstance();
                 dm->SetRaw("shortcut." + id + ".type", json.at("type").get<int>());
                 dm->SetRaw<string>("shortcut." + id + ".param", json.at("param").get<string>());
               });
  server->Get("/api/dialog/browse/:type",
              [](const httplib::Request &req, httplib::Response &res) {
                string t = req.path_params.at("type");
                nlohmann::json response;
                wstring path;
                if (t == "file") {
                  if (LAppPal::BrowseFile(path)) {
                    response["success"] = true;
                    response["path"] = LAppPal::WStringToString(path);
                  } else {
                    response["success"] = false;
                  }
                  res.set_content(response.dump(), "application/json");
                  return;
                }
                if (t == "folder") {
                  if (LAppPal::BrowseFolder(path)) {
                    response["success"] = true;
                    response["path"] = LAppPal::WStringToString(path);
                  } else {
                    response["success"] = false;
                  }
                  res.set_content(response.dump(), "application/json");
                  return;
                }
                response["success"] = false;
                res.set_content(response.dump(), "application/json");
              });
  server->Post("/api/snapshot", [](const httplib::Request &req,
                                          httplib::Response &res) {
      LAppDelegate::GetInstance()->Snapshot();
  });
  server->Get("/api/version", [](const httplib::Request &req,
                                          httplib::Response &res) {
      res.set_header("Cache-Control", "no-store");
      res.set_content(UpdateManager::GetInstance()->Status().dump(), "application/json");
  });
  const auto updateAction = [](auto action) {
    return [action](const httplib::Request& req, httplib::Response& res) {
      // The panel is same-origin. Reject browser requests from other sites.
      const auto origin = req.get_header_value("Origin");
      if (!origin.empty() && origin != "http://127.0.0.1:8053" && origin != "http://localhost:8053") {
        res.status = 403; res.set_content(R"({"error":"请求来源无效"})", "application/json"); return;
      }
      std::string error;
      const bool accepted = (UpdateManager::GetInstance()->*action)(error);
      auto status = UpdateManager::GetInstance()->Status();
      if (!accepted) status["error"] = error;
      res.status = accepted ? 202 : 409;
      res.set_content(status.dump(), "application/json");
    };
  };
  server->Post("/api/update/check", updateAction(&UpdateManager::Check));
  server->Post("/api/update/download", updateAction(&UpdateManager::Download));
  server->Post("/api/update/install", updateAction(&UpdateManager::Install));

  server->Delete("/api/account", [&](const httplib::Request &req,
                                     httplib::Response &res) {
    string cookies = DataManager::GetInstance()->GetWithDefault("cookies", "");
    DataManager::GetInstance()->SetRaw("cookies", string(""));
    DataManager::GetInstance()->SetRaw("uid", string(""));
    httplib::Headers headers = {{"cookie", cookies}};
    const auto bili_jct = CookieValue(cookies, "bili_jct");
    if (!bili_jct.empty()) {
      httplib::SSLClient login_cli("passport.bilibili.com", 443);
      login_cli.set_follow_location(true);
      login_cli.set_connection_timeout(std::chrono::seconds(3));
      login_cli.set_read_timeout(std::chrono::seconds(5));
      auto resp = login_cli.Post(
          "/login/exit/v2", headers, "biliCSRF=" + bili_jct,
          "application/x-www-form-urlencoded");
      if (resp && resp->status == 200) {
        try {
          auto json = nlohmann::json::parse(resp->body);
          int code = json["code"].get<int>();
          if (code == 0) {
            LAppPal::PrintLog(LogLevel::Info, "[PanelServer]Logout successed");
          } else {
            LAppPal::PrintLog(
                LogLevel::Warn,
                "[PanelServer]Logout failed but still reset cookies");
          }
        } catch (const std::exception &e) {
          LAppPal::PrintLog(LogLevel::Error,
                            "[PanelServer]Parse logout failed: %s",
                            resp->body.c_str());
        }
      }
    } else {
      LAppPal::PrintLog(LogLevel::Warn,
                        "[PanelServer]bili_jct not found during logout");
    }
    avatarCache_.ClearAccount();
    BuffManager::GetInstance()->Update();
    res.set_content(R"({"success":true})", "application/json");
  });

  server->Get("/api/account", [this](const httplib::Request &req,
                                 httplib::Response &res) {
    string cookies = DataManager::GetInstance()->GetWithDefault("cookies", "");
    nlohmann::json resp_json = {};
    if (cookies.empty()) {
      avatarCache_.ClearAccount();
      resp_json["login"] = false;
      resp_json["info"] = nlohmann::json::object();
      res.set_content(resp_json.dump(), "application/json");
      return;
    }
    resp_json["login"] = false;
    resp_json["info"] = nlohmann::json::object();

    httplib::Headers headers = {
        {"cookie", cookies},
        {"user-agent",
         "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, "
         "like Gecko) Chrome/128.0.0.0 Safari/537.36"}};
    // nav is the authoritative login check and supplies the current UID.
    // Do not infer account state from the presence of a cookie string.
    httplib::SSLClient client = httplib::SSLClient("api.bilibili.com", 443);
    client.set_connection_timeout(std::chrono::seconds(3));
    client.set_read_timeout(std::chrono::seconds(5));
    auto resp = client.Get("/x/web-interface/nav", headers);
    if (resp && resp->status == 200) {
      try {
        auto json = nlohmann::json::parse(resp->body);
        const auto& data = json.at("data");
        const bool loggedIn = json.at("code").get<int>() == 0 &&
                              data.at("isLogin").get<bool>();
        if (loggedIn && data.contains("mid") && !data.at("mid").is_null() &&
            data.at("mid").get<long long>() > 0) {
          const auto uid = std::to_string(data.at("mid").get<long long>());
          DataManager::GetInstance()->SetRaw("uid", uid);
          DataManager::GetInstance()->SetRaw("uname", data.value("uname", std::string{"用户 "} + uid));
          resp_json["login"] = true;
          resp_json["info"]["uname"] =
              data.value("uname", std::string{});
          resp_json["info"]["uid"] = uid;
          resp_json["info"]["avatar"] =
              avatarCache_.SetAccount(uid, data.value("face", std::string{}));
          resp_json["info"]["level"] = BuffManager::GetInstance()->MedalLevel();
        } else {
          avatarCache_.ClearAccount();
          DataManager::GetInstance()->SetRaw("uid", string(""));
        }
        res.set_content(resp_json.dump(), "application/json");
        return;
      } catch (const std::exception &e) {
        LAppPal::PrintLog("[PanelServer]Parse account nav failed: %s", e.what());
      }
    }
    res.status = 502;
    resp_json["error"] = "账号信息服务暂时不可用";
    res.set_content(resp_json.dump(), "application/json");
  });

  server->Get("/api/account/avatar", [this](const httplib::Request &req,
                                           httplib::Response &res) {
    if (!req.has_param("uid") || !req.has_param("v")) {
      res.set_header("Cache-Control", "no-store");
      res.status = 400;
      return;
    }
    const auto directory = std::filesystem::path(LAppDefine::documentPath) /
                           "cache" / "avatars";
    auto image = avatarCache_.Get(
        req.get_param_value("uid"), req.get_param_value("v"), directory,
        [](const std::string& source) -> std::string {
          std::string host, path;
          if (!BilibiliLogin::ParseHttpsUrl(source, host, path)) return {};
          httplib::SSLClient client(host, 443);
          client.set_connection_timeout(std::chrono::seconds(3));
          client.set_read_timeout(std::chrono::seconds(5));
          // Do not forward login cookies or follow redirects to other hosts.
          httplib::Headers headers = {{"Referer", "https://www.bilibili.com/"}};
          std::string body;
          auto response = client.Get(path, headers,
              [&body](const char* data, size_t size) {
                if (size > AccountAvatarCache::MaxImageSize - body.size())
                  return false;
                body.append(data, size);
                return true;
              });
          if (!response || response->status != 200) return {};
          return body;
        });
    if (image.body.empty()) {
      res.set_header("Cache-Control", "no-store");
      res.status = 404;
      return;
    }
    res.set_header("Cache-Control", "private, max-age=86400, immutable");
    res.set_header("X-Content-Type-Options", "nosniff");
    res.set_content(std::move(image.body), image.contentType);
  });

  server->Get("/api/account/qr", [&](const httplib::Request &req,
                                          httplib::Response &res) {
      httplib::SSLClient login_cli("passport.bilibili.com", 443);
      login_cli.set_follow_location(false);
      login_cli.set_connection_timeout(std::chrono::seconds(3));
      login_cli.set_read_timeout(std::chrono::seconds(5));
      auto resp = login_cli.Get("/x/passport-login/web/qrcode/generate", LoginHeaders());
      if (resp && resp->status == 200) {
        try {
          auto json = nlohmann::json::parse(resp->body);
          const auto qrKey =
              json.at("data").at("qrcode_key").get<std::string>();
          nlohmann::json resp_json = {};
          resp_json["url"] = json.at("data").at("url");
          resp_json["key"] = qrKey;
          res.set_content(resp_json.dump(), "application/json");
          return;
        } catch (const std::exception &e) {
          LAppPal::PrintLog(LogLevel::Warn,
                            "[PanelServer]Parse QR generate failed: %s", e.what());
        }
      }
      if (resp) {
        LAppPal::PrintLog(LogLevel::Warn, "[PanelServer]Get QrCode failed %d", resp->status);
      } else {
        LAppPal::PrintLog(LogLevel::Warn, "[PanelServer]Get QrCode failed");
      }
      res.status = 502;
      res.set_content(R"({"error":"登录服务暂时不可用"})", "application/json");
  });
  server->Get("/api/account/qr-status", [&](const httplib::Request &req,
                                          httplib::Response &res) {
      auto resp_json = nlohmann::json::object();
      const auto currentOauthKey = req.get_param_value("qrcode_key");
      if (currentOauthKey.empty()) {
        res.status = 400;
        resp_json["success"] = false;
        resp_json["error"] = "二维码尚未生成";
        res.set_content(resp_json.dump(), "application/json");
        return;
      }
      httplib::SSLClient login_cli("passport.bilibili.com", 443);
      login_cli.set_follow_location(false);
      login_cli.set_connection_timeout(std::chrono::seconds(3));
      login_cli.set_read_timeout(std::chrono::seconds(5));
      auto resp = login_cli.Get(
          "/x/passport-login/web/qrcode/poll?qrcode_key=" + currentOauthKey,
          LoginHeaders());
      if (!resp || resp->status != 200) {
        res.status = 502;
        resp_json["success"] = false;
        resp_json["error"] = "登录服务暂时不可用";
        res.set_content(resp_json.dump(), "application/json");
        return;
      }
      try {
        auto json = nlohmann::json::parse(resp->body);
        const int code = json.at("data").at("code").get<int>();
        resp_json["code"] = code;
        resp_json["success"] = false;
        if (code == 0) {
          // A complete poll response needs no ticket URL or second request.
          const auto& data = json.at("data");
          const auto url = data.contains("url") && data.at("url").is_string()
                               ? data.at("url").get<std::string>() : std::string{};
          const auto login = ResolveQrLoginCookies(*resp, url);
          if (!login.error.empty()) {
            LAppPal::PrintLog(LogLevel::Warn, "[PanelServer]QR login failed: %s",
                              login.error.c_str());
            res.status = 502;
            resp_json["error"] = "登录成功但未获得完整账号凭据";
          } else {
            DataManager::GetInstance()->SetRaw(
                "cookies", BilibiliLogin::CookieHeader(login.cookies));
            DataManager::GetInstance()->SetRaw("uid", login.cookies.at("DedeUserID"));
            resp_json["success"] = true;
            BuffManager::GetInstance()->Update();
          }
        }
      } catch (const std::exception &e) {
        res.status = 502;
        resp_json["success"] = false;
        resp_json["error"] = "登录响应格式错误";
        LAppPal::PrintLog(LogLevel::Warn,
                          "[PanelServer]Parse QR status failed: %s", e.what());
      }
      res.set_content(resp_json.dump(), "application/json");
  });

  initSSE();
  if (!_stopping) server->listen("127.0.0.1", port);
  LAppPal::PrintLog(LogLevel::Info, "[PanelServer]Worker exit");
}
