#include "PanelServer.hpp"
#include "DataManager.hpp"
#include "GameTask.hpp"
#include "LAppDelegate.hpp"
#include "LAppPal.hpp"
#include "PartStateManager.h"

void PanelServer::Start() {
  // start thread
  std::thread t(&PanelServer::doServe, this);
  t.detach();
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
                               {"cost", task->GetCurrentCost()},
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
  server->Post("/api/attr/:attr",
               [](const httplib::Request &req, httplib::Response &res) {
                 std::string targetAttribute = req.path_params.at("attr");
                 if (targetAttribute.empty()) {
                   res.status = 404;
                   return;
                 }
                 // TODO check valid attribute
                 auto dataManager = DataManager::GetInstance();
                 int currentExperience = dataManager->GetAttribute("exp");
                 int buycnt = dataManager->GetAttribute("buycnt");
                 int currentCost = 10 * pow(1.5, buycnt);
                 if (currentCost > currentExperience) {
                   res.status = 400;
                   return;
                 }
                 dataManager->AddAttribute(targetAttribute, 1);
                 dataManager->AddAttribute("exp", -currentCost);
                 dataManager->AddAttribute("buycnt", 1);
               });
  server->Delete("/api/attr/:attr",
                 [](const httplib::Request &req, httplib::Response &res) {
                   std::string targetAttribute = req.path_params.at("attr");
                   if (targetAttribute.empty()) {
                     res.status = 404;
                     return;
                   }
                   // TODO check valid attribute
                   auto dataManager = DataManager::GetInstance();
                   int buycnt = dataManager->GetAttribute("buycnt");
                   int revertCost = 10 * pow(1.5, max(buycnt - 1, 0)) / 2;
                   dataManager->AddAttribute(targetAttribute, -1);
                   dataManager->AddAttribute("exp", revertCost);
                   dataManager->AddAttribute("buycnt", -1);
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
    json["clothes"]["current"] = dataManager->GetRaw("clothes.current");
    json["clothes"]["unlock"] =
        nlohmann::json::array({true, dataManager->GetRaw("clothes.1.active") == 1,
                               dataManager->GetRaw("clothes.2.active") == 1});
    json["expdiff"] =
        int(1 + ceil(99 * LAppPal::EaseInOut(attributes[4]) / 100));
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
  server->Post("/api/clothes/:id", [](const httplib::Request &req,
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
      unlock = DataManager::GetInstance()->GetRaw("clothes."+ std::to_string(id) + ".active") == 1;
    }
    if (!unlock) {
      LAppPal::PrintLog(LogLevel::Warn, "[PanelServer]Clothes id not active");
      res.status = 400;
      return;
    }
    DataManager::GetInstance()->SetRaw("clothes.current", id);
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
    auto tasks = DataManager::GetInstance()->GetTasks();
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
            DataManager::GetInstance()->AddAttribute(it->first, it->second);
          }
          // if with special, update related key
          if (task->special) {
            DataManager::GetInstance()->SetRaw(task->special->linked_key, 1);
          }
        }
        if (task->repeatable) {
            task->status = TStatus::IDLE;
        } else {
            task->status = TStatus::ARCHIVED;
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
  server->Get("/api/config/audio",
              [](const httplib::Request &req, httplib::Response &res) {
                int volume = LAppDelegate::GetInstance()->GetVolume();
                bool mute = LAppDelegate::GetInstance()->GetMute();
                auto json = nlohmann::json::object();
                json["volume"] = volume;
                json["mute"] = mute;
                res.set_content(json.dump(), "application/json");
              });
  server->Post("/api/config/audio",
               [](const httplib::Request &req, httplib::Response &res) {
                 LAppPal::PrintLog("POST /api/config/audio");
                 try {
                   auto json = nlohmann::json::parse(req.body);
                   LAppDelegate::GetInstance()->SetMute(json.at("mute"));
                   LAppDelegate::GetInstance()->SetVolume(json.at("volume"));
                   LAppDelegate::GetInstance()->SaveSettings();
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

  initSSE();

  server->listen("localhost", 8053);
}
