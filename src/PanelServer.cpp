#include "PanelServer.hpp"
#include "LAppPal.hpp"
#include "LAppDelegate.hpp"
#include "DataManager.hpp"

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

nlohmann::json PanelServer::getTaskStatus() {
  auto tasks = DataManager::GetInstance()->GetTasks();
  std::shared_ptr<GameTask> currentTask;
  // find running task
  for (auto task : tasks) {
    if (task->start_time > 0 && !task->done) {
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
      {"cost", currentTask->cost},
      {"success", currentTask->success},
      {"done", currentTask->done},
      {"requirements", currentTask->requirements},
      {"rewards", currentTask->rewards},
    };
  }
  nlohmann::json taskList = nlohmann::json::array();
  for (auto task : tasks) {
    nlohmann::json taskJson = {
      {"id", task->id},           {"title", LAppPal::WStringToString(task->title)},
      {"desc", LAppPal::WStringToString(task->desc)},       {"start_time", task->start_time},
      {"cost", task->cost},       {"success", task->success},
      {"done", task->done},       {"requirements", task->requirements},
      {"rewards", task->rewards}};
    taskList.push_back(taskJson);
  }
  data["list"] = taskList;
  return data;
}

void PanelServer::doServe() {
  server->set_base_dir("resources/panel/dist");
  server->Get(
      "/api/profile", [](const httplib::Request& req, httplib::Response& res) {
        auto json = nlohmann::json::object();
        json["attributes"] = nlohmann::json::object();
        auto attributes = DataManager::GetInstance()->GetAttributeList();
        json["attributes"]["speed"] = attributes[0];
        json["attributes"]["endurance"] = attributes[1];
        json["attributes"]["strength"] = attributes[2];
        json["attributes"]["will"] = attributes[3];
        json["attributes"]["intellect"] = attributes[4];
        json["attributes"]["exp"] = attributes[5];
        res.set_content(json.dump(), "application/json");
      });
  server->Get(
      "/api/task", [&](const httplib::Request& req, httplib::Response& res) {
        LAppPal::PrintLog(LogLevel::Debug, "GET /api/task");
        try {
          auto data = getTaskStatus();
          res.set_content(data.dump(), "application/json");
        } catch (const std::exception& e) {
          res.status = 500;
          res.set_content(e.what(), "text/plain");
          LAppPal::PrintLog(LogLevel::Error, e.what());
        }
      });
  server->Post("/api/task/:id/start",
               [&](const httplib::Request& req, httplib::Response& res) {
                 LAppPal::PrintLog(LogLevel::Debug, "POST /api/task/:id/start");
                 int id = std::stoi(req.path_params.at("id"));
                 auto tasks = DataManager::GetInstance()->GetTasks();
                 for (auto task : tasks) {
                   // cancel current task if it is running
                   if (task->id != id && task->start_time > 0 && !task->done) {
                     task->start_time = 0;
                   }
                   if (task->id == id) {
                     // if task is done and success, return 400
                     if (task->done && task->success) {
                       res.status = 400;
                       return;
                     }
                     task->start_time = time(nullptr);
                     task->done = false;
                     auto data = getTaskStatus();
                     res.set_content(data.dump(), "application/json");
                     return;
                   }
                 }
                 res.status = 404;
               });
  server->Post("/api/task/:id/cancel",
               [&](const httplib::Request& req, httplib::Response& res) {
                 LAppPal::PrintLog(LogLevel::Debug, "POST /api/task/:id/cancel");
                 int id = std::stoi(req.path_params.at("id"));
                 auto tasks = DataManager::GetInstance()->GetTasks();
                 for (auto task : tasks) {
                   if (task->id == id && !task->success) {
                     task->start_time = 0;
                     task->done = false;
                     auto data = getTaskStatus();
                     res.set_content(data.dump(), "application/json");
                     return;
                   }
                 }
                 res.status = 404;
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
