#include "PanelServer.hpp"
#include "BuffManager.hpp"
#include "CloudGame.hpp"
#include "DataManager.hpp"
#include "GameTask.hpp"
#include "LAppDefine.hpp"
#include "LAppPal.hpp"
#include "PartStateManager.h"
#include "LAppDelegate.hpp"
#include "Wbi.hpp"
#include "Platform.hpp"
#include "UpdateManager.hpp"

#include <map>
#include <string_view>

namespace {

constexpr int kQrRedirectLimit = 5;

std::string UrlDecode(std::string_view value) {
  std::string decoded;
  decoded.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%' && i + 2 < value.size()) {
      const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      const int high = hex(value[i + 1]);
      const int low = hex(value[i + 2]);
      if (high >= 0 && low >= 0) {
        decoded.push_back(static_cast<char>((high << 4) | low));
        i += 2;
        continue;
      }
    }
    decoded.push_back(value[i] == '+' ? ' ' : value[i]);
  }
  return decoded;
}

bool IsLoginCookie(std::string_view name) {
  return name == "DedeUserID" || name == "DedeUserID__ckMd5" ||
         name == "SESSDATA" || name == "bili_jct" || name == "sid";
}

void AddCookie(std::map<std::string, std::string>& cookies,
               std::string_view value) {
  const auto end = value.find(';');
  const auto pair = value.substr(0, end);
  const auto separator = pair.find('=');
  if (separator == std::string_view::npos) return;
  const auto name = pair.substr(0, separator);
  if (!IsLoginCookie(name)) return;
  const auto cookieName = std::string(name);
  cookies[cookieName] = std::string(pair.substr(separator + 1));
}

void AddCookiesFromQuery(std::map<std::string, std::string>& cookies,
                         std::string_view url) {
  const auto queryStart = url.find('?');
  if (queryStart == std::string_view::npos) return;
  auto query = url.substr(queryStart + 1);
  const auto fragmentStart = query.find('#');
  if (fragmentStart != std::string_view::npos) query = query.substr(0, fragmentStart);
  while (!query.empty()) {
    const auto separator = query.find('&');
    const auto item = query.substr(0, separator);
    const auto equals = item.find('=');
    if (equals != std::string_view::npos) {
      const auto name = UrlDecode(item.substr(0, equals));
      if (IsLoginCookie(name)) {
        // Keep the value encoded as returned by the ticket URL. Cookie values
        // such as SESSDATA may legitimately contain percent escapes.
        cookies[name] = std::string(item.substr(equals + 1));
      }
    }
    if (separator == std::string_view::npos) break;
    query.remove_prefix(separator + 1);
  }
}

bool ParseHttpsUrl(const std::string& url, std::string& host,
                   std::string& path) {
  constexpr std::string_view prefix = "https://";
  if (url.compare(0, prefix.size(), prefix) != 0) return false;
  const auto authorityStart = prefix.size();
  const auto authorityEnd = url.find_first_of("/?#", authorityStart);
  const auto authority = url.substr(
      authorityStart, authorityEnd == std::string::npos
                          ? std::string::npos
                          : authorityEnd - authorityStart);
  if (authority.empty() || authority.find(':') != std::string::npos) return false;
  host = authority;
  if (authorityEnd == std::string::npos) {
    path = "/";
  } else if (url[authorityEnd] == '?') {
    path = "/" + url.substr(authorityEnd);
  } else if (url[authorityEnd] == '#') {
    path = "/";
  } else {
    path = url.substr(authorityEnd);
  }
  const auto fragment = path.find('#');
  if (fragment != std::string::npos) path.resize(fragment);
  return true;
}

bool IsAllowedLoginHost(const std::string& host) {
  return host == "passport.bilibili.com" ||
         host == "passport-api.bilibili.com" ||
         host == "account.bilibili.com" || host == "www.bilibili.com" ||
         host == "bilibili.com";
}

std::string CookieHeader(
    const std::map<std::string, std::string>& cookies) {
  std::string result;
  for (const auto& [name, value] : cookies) {
    if (!result.empty()) result += "; ";
    result += name + "=" + value;
  }
  return result;
}

bool HasLoginCookies(const std::map<std::string, std::string>& cookies) {
  const auto hasValue = [&cookies](const char* name) {
    const auto it = cookies.find(name);
    return it != cookies.end() && !it->second.empty();
  };
  return hasValue("DedeUserID") && hasValue("SESSDATA") &&
         hasValue("bili_jct");
}

bool IsValidUid(const std::string& uid) {
  if (uid.empty()) return false;
  for (const char c : uid) {
    if (c < '0' || c > '9') return false;
  }
  return uid != "0";
}

// The QR poll endpoint returns a ticket URL, not a Cookie header. Resolve that
// URL like a browser and retain the Set-Cookie values from every redirect.
std::string ResolveQrLoginCookies(const std::string& loginUrl) {
  std::map<std::string, std::string> cookies;
  AddCookiesFromQuery(cookies, loginUrl);
  std::string currentUrl = loginUrl;

  for (int redirectCount = 0; redirectCount <= kQrRedirectLimit;
       ++redirectCount) {
    std::string host;
    std::string path;
    if (!ParseHttpsUrl(currentUrl, host, path)) {
      LAppPal::PrintLog(LogLevel::Warn,
                        "[PanelServer]QR login returned an invalid HTTPS URL");
      return {};
    }
    if (!IsAllowedLoginHost(host)) {
      LAppPal::PrintLog(LogLevel::Warn,
                        "[PanelServer]QR login redirected outside Bilibili");
      return {};
    }

    httplib::SSLClient client(host, 443);
    client.set_follow_location(false);
    client.set_connection_timeout(std::chrono::seconds(3));
    client.set_read_timeout(std::chrono::seconds(5));
    httplib::Headers headers = {
        {"Referer", "https://passport.bilibili.com/"}};
    const auto existingCookies = CookieHeader(cookies);
    if (!existingCookies.empty()) headers.emplace("Cookie", existingCookies);
    auto response = client.Get(path, headers);
    if (!response) {
      LAppPal::PrintLog(LogLevel::Warn,
                        "[PanelServer]QR login cookie exchange failed");
      return {};
    }
    const auto setCookieCount = response->get_header_value_count("Set-Cookie");
    for (size_t i = 0; i < setCookieCount; ++i) {
      AddCookie(cookies, response->get_header_value("Set-Cookie", i));
    }
    if (setCookieCount > 0 && HasLoginCookies(cookies)) {
      return CookieHeader(cookies);
    }

    if (response->status < 300 || response->status >= 400 ||
        !response->has_header("Location") ||
        redirectCount == kQrRedirectLimit) {
      break;
    }
    auto location = response->get_header_value("Location");
    if (location.compare(0, 2, "//") == 0) {
      currentUrl = "https:" + location;
    } else if (location.compare(0, 8, "https://") == 0) {
      currentUrl = location;
    } else if (!location.empty() && location[0] == '/') {
      currentUrl = "https://" + host + location;
    } else if (!location.empty()) {
      const auto query = path.find('?');
      const auto pathOnly = path.substr(0, query);
      const auto slash = pathOnly.rfind('/');
      currentUrl = "https://" + host +
                   pathOnly.substr(0, slash == std::string::npos ? 0 : slash + 1) +
                   location;
    } else {
      LAppPal::PrintLog(LogLevel::Warn,
                        "[PanelServer]QR login returned an invalid redirect");
      return {};
    }
  }

  return HasLoginCookies(cookies) ? CookieHeader(cookies) : std::string{};
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
  return DataManager::GetInstance()->GetTaskState();
}

void PanelServer::doServe() {
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
  auto gameAction = [this](httplib::Response& res, const nlohmann::json& action) {
    auto error = CloudGame::GetInstance()->Command(action);
    res.status = error.empty() ? 200 : 409;
    res.set_content(error.empty() ? DataManager::GetInstance()->GetCloudProfile().dump()
        : nlohmann::json{{"error", error}}.dump(), "application/json");
  };
  server->Post("/api/star", [gameAction](const auto&, auto& res) { gameAction(res, {{"type", "star"}}); });
  server->Post("/api/attr/:attr", [gameAction](const auto& req, auto& res) { gameAction(res, {{"type", "attr.buy"}, {"attr", req.path_params.at("attr")}}); });
  server->Delete("/api/attr/:attr", [gameAction](const auto& req, auto& res) { gameAction(res, {{"type", "attr.refund"}, {"attr", req.path_params.at("attr")}}); });
  server->Get("/api/profile", [](const auto&, auto& res) {
    res.set_content(DataManager::GetInstance()->GetCloudProfile().dump(), "application/json");
  });
  server->Get("/api/cloud", [](const auto&, auto& res) {
    auto state = CloudGame::GetInstance()->Status();
    state["url"] = CloudGame::ServiceUrl();
    res.set_content(state.dump(), "application/json");
  });
  server->Post("/api/cloud/reconnect", [](const auto& req, auto& res) {
    try {
      const auto payload = nlohmann::json::parse(req.body);
      CloudGame::GetInstance()->Wake(payload.value("take_over", false));
      res.set_content("{\"success\":true}", "application/json");
    } catch (const std::exception& e) {
      res.status = 400;
      res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
    }
  });
  server->Get("/api/rank", [](const auto& req, auto& res) {
    try {
      const auto url = CloudGame::ServiceUrl();
      const auto split = url.find('/', url.find("://") + 3);
      auto prefix = split == std::string::npos ? std::string{} : url.substr(split);
      while (!prefix.empty() && prefix.back() == '/') prefix.pop_back();
      httplib::Client client(split == std::string::npos ? url : url.substr(0, split));
      client.set_connection_timeout(2, 0); client.set_read_timeout(4, 0);
      const auto metric = req.has_param("metric") ? req.get_param_value("metric") : "starcnt";
      const auto offset = req.has_param("offset") ? req.get_param_value("offset") : "0";
      if (metric != "starcnt" && metric != "exp" && metric != "attr") throw std::runtime_error("无效榜单");
      if (offset.empty() || offset.size() > 5 || offset.find_first_not_of("0123456789") != std::string::npos) throw std::runtime_error("无效分页");
      const auto uid = DataManager::GetInstance()->GetWithDefault("uid", std::string{});
      auto response = client.Get(prefix + "/v1/rank?metric=" + metric + "&offset=" + offset + "&uid=" + uid);
      if (!response) throw std::runtime_error("排行榜连接失败，请稍后重试");
      res.status = response->status;
      res.set_content(response->body, "application/json");
    } catch (const std::exception& e) {
      res.status = 503; res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
    }
  });
  server->Get("/api/achievements", [](const httplib::Request&, httplib::Response& res) {
    try {
      res.set_content(DataManager::GetInstance()->GetAchievementState().dump(), "application/json");
    } catch (const std::exception& e) {
      LAppPal::PrintLog(LogLevel::Error, "[Achievements]Load failed: %s", e.what());
      res.status = 500;
      res.set_content("{\"error\":\"成就加载失败，请重试\"}", "application/json");
    }
  });
  server->Post("/api/data/reset", [](const httplib::Request &req, httplib::Response &res) {
    auto error = CloudGame::GetInstance()->Command({{"type", "reset"}});
    res.status = error.empty() ? 200 : 409;
    res.set_content(error.empty() ? "{\"success\":true}" : nlohmann::json{{"error", error}}.dump(), "application/json");
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
    auto error = CloudGame::GetInstance()->Command({{"type", "clothes"}, {"id", id}});
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
      auto dm = DataManager::GetInstance();
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
    taskResponse(res, [] { return DataManager::GetInstance()->UpgradeTaskQueue(); });
  });
  server->Post("/api/task/:id/start", [taskResponse](const httplib::Request& req,
                                                   httplib::Response& res) {
    taskResponse(res, [&] {
      return DataManager::GetInstance()->StartTask(std::stoi(req.path_params.at("id")));
    });
  });
  server->Post("/api/task/:id/queue", [taskResponse](const httplib::Request& req,
                                                   httplib::Response& res) {
    taskResponse(res, [&] {
      return DataManager::GetInstance()->QueueTask(std::stoi(req.path_params.at("id")));
    });
  });
  server->Delete("/api/task/queue/:entryId", [taskResponse](const httplib::Request& req,
                                                         httplib::Response& res) {
    taskResponse(res, [&] {
      return DataManager::GetInstance()->RemoveQueuedTask(std::stoll(req.path_params.at("entryId")));
    });
  });
  server->Post("/api/task/queue/:entryId/move", [taskResponse](const httplib::Request& req,
                                                           httplib::Response& res) {
    taskResponse(res, [&] {
      auto payload = nlohmann::json::parse(req.body);
      return DataManager::GetInstance()->MoveQueuedTask(
          std::stoll(req.path_params.at("entryId")), payload.at("direction").get<int>());
    });
  });
  server->Post("/api/task/:id/cancel", [taskResponse](const httplib::Request& req,
                                                    httplib::Response& res) {
    taskResponse(res, [&] {
      return DataManager::GetInstance()->CancelTask(std::stoi(req.path_params.at("id")));
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
    CloudGame::GetInstance()->Disconnect();
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
      resp_json["info"] = {{"confirm", false}};
      res.set_content(resp_json.dump(), "application/json");
      return;
    }
    resp_json["login"] = false;
    resp_json["info"] = nlohmann::json::object();
    resp_json["info"]["confirm"] =
        DataManager::GetInstance()->GetWithDefault("data-share", 0) == 1;

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
          const auto previousUid = DataManager::GetInstance()->GetWithDefault("uid", std::string{});
          DataManager::GetInstance()->SetRaw("uid", uid);
          DataManager::GetInstance()->SetRaw("uname", data.value("uname", std::string{"用户 "} + uid));
          if (previousUid != uid) CloudGame::GetInstance()->Wake();
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
          if (!ParseHttpsUrl(source, host, path)) return {};
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
      login_cli.set_follow_location(true);
      login_cli.set_connection_timeout(std::chrono::seconds(3));
      login_cli.set_read_timeout(std::chrono::seconds(5));
      auto resp = login_cli.Get("/x/passport-login/web/qrcode/generate");
      if (resp && resp->status == 200) {
        try {
          auto json = nlohmann::json::parse(resp->body);
          const auto qrKey =
              json.at("data").at("qrcode_key").get<std::string>();
          LAppPal::PrintLog(LogLevel::Debug, "[PanelServer]Get QrCode oauth %s",
                            qrKey.c_str());
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
      login_cli.set_follow_location(true);
      login_cli.set_connection_timeout(std::chrono::seconds(3));
      login_cli.set_read_timeout(std::chrono::seconds(5));
      auto resp = login_cli.Get(
          "/x/passport-login/web/qrcode/poll?qrcode_key=" + currentOauthKey);
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
          const auto url = json.at("data").at("url").get<std::string>();
          const auto cookies = ResolveQrLoginCookies(url);
          const auto uid = CookieValue(cookies, "DedeUserID");
          if (cookies.empty() || !IsValidUid(uid)) {
            res.status = 502;
            resp_json["error"] = "登录成功但未获得完整账号凭据";
          } else {
            DataManager::GetInstance()->SetRaw("cookies", cookies);
            DataManager::GetInstance()->SetRaw("uid", uid);
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
  server->Post("/api/account/share",
               [](const httplib::Request &req, httplib::Response &res) {
                 auto error = CloudGame::GetInstance()->Command({{"type", "share"}, {"enabled", true}});
                 res.status = error.empty() ? 200 : 409;
                 res.set_content(error.empty() ? "{\"success\":true}" : nlohmann::json{{"error", error}}.dump(), "application/json");
               });
  server->Delete("/api/account/share", [](const auto&, auto& res) {
    auto error = CloudGame::GetInstance()->Command({{"type", "share"}, {"enabled", false}});
    res.status = error.empty() ? 200 : 409;
    res.set_content(error.empty() ? "{\"success\":true}" : nlohmann::json{{"error", error}}.dump(), "application/json");
  });

  initSSE();
  if (!_stopping) server->listen("127.0.0.1", 8053);
  LAppPal::PrintLog(LogLevel::Info, "[PanelServer]Worker exit");
}
