#include "PanelServer.hpp"
#include "BuffManager.hpp"
#include "DataManager.hpp"
#include "GameTask.hpp"
#include "LAppDefine.hpp"
#include "LAppPal.hpp"
#include "PartStateManager.h"
#include "LAppDelegate.hpp"
#include "Wbi.hpp"

#include <shellapi.h>
#include <winuser.h>

void PanelServer::Start() {
  // start thread
  worker_ = std::thread(&PanelServer::doServe, this);
  worker_.detach();
}

void PanelServer::DataSinkHandle(httplib::DataSink &sink) {
  std::unique_lock<std::mutex> lock(_mtx);
  int id = _messageId + 1;
  _cv.wait(lock, [&] { return _messageId == id; });
  sink.write(_message.c_str(), _message.size());
}

void PanelServer::Notify(const std::string &message) {
  std::lock_guard<std::mutex> lock(_mtx);
  _message = "data: " + message + "\n\n";
  _messageId++;
  _cv.notify_all();
}

void PanelServer::initSSE() {
  // client send a request and wait for response
  // if any message is sent to the client, the client will wait for response
  // again so we get a connection to notify the client
  server->Get("/api/sse", [this](const httplib::Request &req,
                                 httplib::Response &res) {
    LAppPal::PrintLog(LogLevel::Debug, "GET /api/sse");
    res.set_chunked_content_provider(
        "text/event-stream", [&](size_t /*offset*/, httplib::DataSink &sink) {
          // this will block until server wants to send message
          DataSinkHandle(sink);
          return true;
        });
  });
}

nlohmann::json PanelServer::getTaskStatus() {
  auto tasks = DataManager::GetInstance()->GetTasks();
  std::shared_ptr<GameTask> currentTask;
  // find current task
  for (auto task : tasks) {
    if (task->status == TStatus::RUNNING ||
        task->status == TStatus::WAIT_SETTLE) {
      currentTask = task;
      break;
    }
  }
  nlohmann::json data = nlohmann::json::object();
  if (currentTask) {
    // filter out current task
    tasks.erase(std::remove(tasks.begin(), tasks.end(), currentTask),
                tasks.end());
    data["current"] = {
        {"id", currentTask->id},
        {"title", LAppPal::WStringToString(currentTask->title)},
        {"desc", LAppPal::WStringToString(currentTask->desc)},
        {"start_time", currentTask->start_time},
        {"end_time", currentTask->end_time},
        {"cost", currentTask->cost_snapshot},
        {"success", currentTask->success},
        {"status", currentTask->status},
        {"repeatable", currentTask->repeatable},
        {"requirements", currentTask->requirements},
        {"rewards", currentTask->rewards},
    };
    if (currentTask->special) {
      data["current"]["special"] = nlohmann::json::object();
      data["current"]["special"]["title"] = LAppPal::WStringToString(currentTask->special->title);
      data["current"]["special"]["desc"] = LAppPal::WStringToString(currentTask->special->desc);
    }
  }
  nlohmann::json taskList = nlohmann::json::array();
  for (auto task : tasks) {
    nlohmann::json taskJson = {{"id", task->id},
                               {"title", LAppPal::WStringToString(task->title)},
                               {"desc", LAppPal::WStringToString(task->desc)},
                               {"start_time", task->start_time},
                               {"end_time", task->end_time},
                               {"cost", task->cost},
                               {"success", task->success},
                               {"status", task->status},
                               {"requirements", task->requirements},
                               {"rewards", task->rewards},
                               {"repeatable", task->repeatable}};
    if (task->special) {
      taskJson["special"] = nlohmann::json::object();
      taskJson["special"]["title"] = LAppPal::WStringToString(task->special->title);
      taskJson["special"]["desc"] = LAppPal::WStringToString(task->special->desc);
    }
    taskList.push_back(taskJson);
  }
  data["list"] = taskList;
  return data;
}

void PanelServer::doServe() {
  server->set_base_dir("resources/panel/dist");
  server->Post("/api/star",
               [&](const httplib::Request &req, httplib::Response &res) {
                 DataManager::GetInstance()->FetchStar();
                 Notify("UPDATE");
               });
  server->Post("/api/attr/:attr",
               [&](const httplib::Request &req, httplib::Response &res) {
                 std::string targetAttribute = req.path_params.at("attr");
                 if (targetAttribute.empty()) {
                   res.status = 404;
                   return;
                 }
                 // TODO check valid attribute
                 auto dataManager = DataManager::GetInstance();
                 // cannot add attributes to more than limit
                 if (dataManager->GetAttribute(targetAttribute) >= dataManager->GetAttrLimit()) {
                   res.status = 405;
                   return;
                 }
                 int currentExperience = dataManager->GetAttribute("exp");
                 int buycnt = dataManager->GetAttribute("buycnt");
                 int currentCost = 53000;
                 if (buycnt < 25) {
                   currentCost = std::ceilf(10.0f * pow(1.41, buycnt));
                 }
                 if (currentCost > currentExperience) {
                   res.status = 400;
                   return;
                 }
                 dataManager->AddAttribute(targetAttribute, 1);
                 dataManager->AddAttribute("exp", -currentCost);
                 dataManager->AddAttribute("buycnt", 1);
                 Notify("UPDATE");
               });
  server->Delete("/api/attr/:attr",
                 [&](const httplib::Request &req, httplib::Response &res) {
                   std::string targetAttribute = req.path_params.at("attr");
                   if (targetAttribute.empty()) {
                     res.status = 404;
                     return;
                   }
                   // TODO check valid attribute
                   auto dataManager = DataManager::GetInstance();
                   int buycnt = dataManager->GetAttribute("buycnt");
                   int last_cost = 53000;
                   // 10 * 1.41^25 = 53762
                   if (buycnt < 26) {
                     last_cost = std::ceilf(10.0f * pow(1.41, max(buycnt - 1, 0)));
                   }
                   LAppPal::PrintLog(LogLevel::Debug, "[PanelServer]Revert attr buycnt=%d last_cost=%d", buycnt, last_cost);
                   int revertCost = last_cost / 2;
                   dataManager->AddAttribute(targetAttribute, -1);
                   dataManager->AddAttribute("exp", revertCost);
                   dataManager->AddAttribute("buycnt", -1);
                   Notify("UPDATE");
                 });
  server->Get("/api/profile", [](const httplib::Request &req,
                                 httplib::Response &res) {
    auto json = nlohmann::json::object();
    json["attributes"] = nlohmann::json::object();
    auto dataManager = DataManager::GetInstance();
    auto attributes = dataManager->GetAttributeList();
    json["attributes"]["speed"] = attributes[0];
    json["attributes"]["endurance"] = attributes[1];
    json["attributes"]["strength"] = attributes[2];
    json["attributes"]["will"] = attributes[3];
    json["attributes"]["intellect"] = attributes[4];
    json["attributes"]["exp"] = attributes[5];
    json["attributes"]["buycnt"] = attributes[6];
    json["clothes"]["current"] = dataManager->GetWithDefault("clothes.current", 0);
    json["clothes"]["unlock"] =
        nlohmann::json::array({true, dataManager->GetWithDefault("clothes.1.active", 0) == 1,
                               dataManager->GetWithDefault("clothes.2.active", 0) == 1});
    json["expdiff"] = dataManager->CurrentExpDiff();
    json["buffs"] = nlohmann::json::array();
    json["starcnt"] = dataManager->GetWithDefault("starcnt", 0);
    auto buffs_array = BuffManager::GetInstance()->GetBuffList();
    for (const auto& buff : buffs_array) {
      json["buffs"].push_back(buff);
    }
    res.set_content(json.dump(), "application/json");
  });
  server->Post("/api/data/reset", [](const httplib::Request &req, httplib::Response &res) {
    DataManager::GetInstance()->SetResetMark();
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
    // check id valid, 0 is actived by default
    bool unlock = true;
    if (id > 0) {
      unlock = DataManager::GetInstance()->GetWithDefault("clothes."+ std::to_string(id) + ".active", 0) == 1;
    }
    if (!unlock) {
      LAppPal::PrintLog(LogLevel::Warn, "[PanelServer]Clothes id not active");
      res.status = 400;
      return;
    }
    DataManager::GetInstance()->SetRaw("clothes.current", id);
    Notify("UPDATE");
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
  server->Post("/api/task/:id/start", [&](const httplib::Request &req,
                                          httplib::Response &res) {
    LAppPal::PrintLog(LogLevel::Debug, "POST /api/task/:id/start");
    int id = std::stoi(req.path_params.at("id"));
    auto tasks = DataManager::GetInstance()->GetTasks();
    std::shared_ptr<GameTask> targetTask;
    for (auto task : tasks) {
      // cannot start a new task while old one is running
      if (task->status == TStatus::RUNNING ||
          task->status == TStatus::WAIT_SETTLE) {
        LAppPal::PrintLog(LogLevel::Warn,
                          "[PanelServer]Already exists a running task");
        res.status = 400;
        return;
      }
      if (task->id == id) {
        targetTask = task;
      }
    }
    if (targetTask) {
        if (targetTask->status == TStatus::ARCHIVED) {
          // obviously archived task cannot be started
          res.status = 401;
          return;
        }
        // do starting work
        targetTask->start_time = time(nullptr);
        targetTask->success = false;
        targetTask->status = TStatus::RUNNING;
        targetTask->cost_snapshot = targetTask->GetCurrentCost();
        targetTask->Dump();
        auto data = getTaskStatus();
        res.set_content(data.dump(), "application/json");
        return;
    }
    res.status = 401;
  });
  server->Post("/api/task/:id/confirm", [&](const httplib::Request &req,
                                            httplib::Response &res) {
    LAppPal::PrintLog(LogLevel::Debug, "POST /api/task/:id/confirm");
    int id = std::stoi(req.path_params.at("id"));
    auto dataManager = DataManager::GetInstance();
    auto tasks = dataManager->GetTasks();
    for (auto task : tasks) {
      if (task->id == id) {
        if (task->status != TStatus::WAIT_SETTLE) {
          res.status = 400;
          return;
        }
        // complete this task
        task->end_time = time(nullptr);
        if (task->success) {
          // update attribute
          for (auto it = task->rewards.begin(); it != task->rewards.end();
               ++it) {
            dataManager->AddAttribute(it->first, it->second);
          }
          if (task->id == 1) {
            dataManager->AddAttribute("exp", 10 * dataManager->CurrentExpDiff());
          }
          // if with special, update related key
          if (task->special) {
            dataManager->SetRaw(task->special->linked_key, 1);
          }
          dataManager->SetRaw("buff.failcount", 0);
          LAppPal::PrintLog("[PanelServer]Failcount set to 0");
          Notify("UPDATE");
        } else {
          int failcount = dataManager->GetWithDefault("buff.failcount", 0);
          failcount++;
          dataManager->SetRaw("buff.failcount", failcount);
          LAppPal::PrintLog("[PanelServer]Failcount set to %d", failcount);
        }
        if (task->repeatable) {
            task->status = TStatus::IDLE;
        } else {
            task->status = task->success ? TStatus::ARCHIVED : TStatus::IDLE;
        }
        task->Dump();
        auto data = getTaskStatus();
        res.set_content(data.dump(), "application/json");
        return;
      }
    }
    res.status = 404;
  });
  server->Post("/api/task/:id/cancel", [&](const httplib::Request &req,
                                           httplib::Response &res) {
    LAppPal::PrintLog(LogLevel::Debug, "POST /api/task/:id/cancel");
    int id = std::stoi(req.path_params.at("id"));
    auto tasks = DataManager::GetInstance()->GetTasks();
    for (auto task : tasks) {
      if (task->id == id) {
        if (task->status != TStatus::RUNNING) {
          res.status = 400;
          return;
        }
        task->start_time = 0;
        task->success = false;
        task->status = TStatus::IDLE;
        task->Dump();
        auto data = getTaskStatus();
        res.set_content(data.dump(), "application/json");
        return;
      }
    }
    res.status = 404;
  });
  server->Post("/api/config/folder", [](const httplib::Request &req, httplib::Response &res) {
    ShellExecute(NULL, L"open", LAppDefine::documentPath.c_str(), NULL, NULL, SW_SHOWDEFAULT);
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
    DataManager::GetInstance()->UpdateNotify(json.at("live"), json.at("dynamic"), json.at("update"));
    LAppDelegate::GetInstance()->LiveNotify = json.at("live");
    LAppDelegate::GetInstance()->DynamicNotify = json.at("dynamic");
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

  httplib::SSLClient login_cli("passport.bilibili.com", 443);
  login_cli.set_follow_location(true);
  login_cli.enable_server_certificate_verification(false);
  login_cli.set_connection_timeout(std::chrono::seconds(1));
  std::string oauth_key;

  server->Delete("/api/account", [&](const httplib::Request &req,
                                     httplib::Response &res) {
    string cookies = DataManager::GetInstance()->GetWithDefault("cookies", "");
    httplib::Headers headers = {{"cookie", cookies}};
    // extract bili_jct from cookies
    
    std::regex pattern("bili_jct=([a-z0-9]+)");
    std::smatch match;
    std::regex_search(cookies, match, pattern);
    if (match.size() < 2) {
      LAppPal::PrintLog(LogLevel::Error, "[PanelServer]bili_jct not found");
      return;
    }
    string bili_jct = match[1];

    auto resp = login_cli.Post("/login/exit/v2", headers, "biliCSRF=" + bili_jct, "application/x-www-form-urlencoded");
    if (resp && resp->status == 200) {
      try {
        auto json = nlohmann::json::parse(resp->body);
        int code = json["code"].get<int>();
        if (code == 0) {
          LAppPal::PrintLog(LogLevel::Info, "[PanelServer]Logout successed");
        } else {
          LAppPal::PrintLog(LogLevel::Warn, "[PanelServer]Logout failed but still reset cookies");
        }
      } catch (const std::exception &e) {
        LAppPal::PrintLog(LogLevel::Error,
                          "[PanelServer]Parse logout failed: %s",
                          resp->body.c_str());
      }
    }
    DataManager::GetInstance()->SetRaw("cookies", string(""));
  });

  server->Get("/api/account", [](const httplib::Request &req,
                                 httplib::Response &res) {
    string cookies = DataManager::GetInstance()->GetWithDefault("cookies", "");
    nlohmann::json resp_json = {};
    if (cookies.empty()) {
      resp_json["login"] = false;
      res.set_content(resp_json.dump(), "application/json");
      return;
    }
    resp_json["login"] = true;
    resp_json["info"] = nlohmann::json::object();
    resp_json["info"]["level"] = BuffManager::GetInstance()->MedalLevel();

    httplib::Headers headers = {{"cookie", cookies}};
    httplib::SSLClient client = httplib::SSLClient("api.bilibili.com", 443);
    client.set_connection_timeout(std::chrono::seconds(1));

    // get uid from cookies string, find DedeUserID
    std::regex pattern("DedeUserID=([0-9]+)");
    std::smatch match;
    std::regex_search(cookies, match, pattern);
    if (match.size() < 2) {
      resp_json["login"] = false;
      res.set_content(resp_json.dump(), "application/json");
      return;
    }
    string uid = match[1];

    DataManager::GetInstance()->SetRaw("uid", uid);

    string request_path = "/x/space/wbi/acc/info?";
    nlohmann::json Params;
    Params["mid"] = uid;

    auto wbi_config = LAppDelegate::GetInstance()->GetUserStateManager()->GetWbiKey();
    if (wbi_config) {
      const auto mixin_key = Wbi::Get_mixin_key(wbi_config->img_key, wbi_config->sub_key);
      const auto w_rid = Wbi::Calc_sign(Params, mixin_key);
      request_path += Wbi::Json_to_url_encode_str(Params) + "&w_rid=" + w_rid;
    } else {
      request_path += Wbi::Json_to_url_encode_str(Params);
    }

    auto resp = client.Get(request_path.c_str(), headers);
    if (resp && resp->status == 200) {
      auto json = nlohmann::json::parse(resp->body);
      if (json.at("code") == 0 && json.contains("data")) {
        try {
          resp_json["info"]["uname"] =
              json.at("data").at("name").get<std::string>();
        } catch (const std::exception &e) {
          LAppPal::PrintLog("[PanelServer]Exception: %s", e.what());
        }
        res.set_content(resp_json.dump(), "application/json");
        return;
      } else {
        res.status = 500;
        LAppPal::PrintLog("[PanelServer]Fetch account info failed with code %d",
                          json.at("code").get<int>());
      }
    } else {
      res.status = 500;
      LAppPal::PrintLog("[PanelServer]Fetch account info failed");
    }
  });

  server->Get("/api/account/qr", [&](const httplib::Request &req,
                                          httplib::Response &res) {
      auto resp = login_cli.Get("/x/passport-login/web/qrcode/generate");
      if (resp && resp->status == 200) {
        auto json = nlohmann::json::parse(resp->body);
        oauth_key = json["data"]["qrcode_key"];
        LAppPal::PrintLog(LogLevel::Debug, "[PanelServer]Get QrCode oauth %s", oauth_key.c_str());
        nlohmann::json resp_json = {};
        resp_json["url"] = json["data"]["url"];
        res.set_content(resp_json.dump(), "application/json");
        return;
      }
      if (resp) {
        LAppPal::PrintLog(LogLevel::Warn, "[PanelServer]Get QrCode failed %d", resp->status);
      } else {
        LAppPal::PrintLog(LogLevel::Warn, "[PanelServer]Get QrCode failed");
      }
  });
  server->Get("/api/account/qr-status", [&](const httplib::Request &req,
                                          httplib::Response &res) {
      auto resp = login_cli.Get("/x/passport-login/web/qrcode/poll?qrcode_key=" + oauth_key);
      auto json = nlohmann::json::parse(resp->body);
      auto resp_json = nlohmann::json::object();
      if (json["data"]["code"] == 0) {
        resp_json["success"] = true;
        // get cookies from url param
        std::string url = json["data"]["url"].get<std::string>();
        std::string queryString = url.substr(url.find('?') + 1);
        // replace & as ;
        queryString = std::regex_replace(queryString, std::regex("&"), ";");
        DataManager::GetInstance()->SetRaw("cookies", queryString);
        BuffManager::GetInstance()->Update();
      } else {
        resp_json["success"] = false;
      }
      res.set_content(resp_json.dump(), "application/json");
  });

  initSSE();
  server->listen("localhost", 8053);
  LAppPal::PrintLog(LogLevel::Info, "[PanelServer]Worker exit");
}
