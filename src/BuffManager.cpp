#include "BuffManager.hpp"
#include "DataManager.hpp"
#include "LAppPal.hpp"
#include "Wbi.hpp"

#include <httplib.h>

void BuffManager::thread() {
  time_t last = 0;
  while(running_) {
    time_t now = time(nullptr);
    if (now - last >= 10) {
      last = now;
      Update();
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  LAppPal::PrintLog(LogLevel::Info, "[BuffManager]Worker exit");
}

void BuffManager::Update() {
  DataManager* dm = DataManager::GetInstance();
  auto cookies = dm->GetWithDefault("cookies", "");
  auto user_agent = dm->GetWithDefault("user-agent", "");
  if (cookies.empty()) {
    // not login, just skip
    return;
  }
  httplib::Headers headers = {{"cookie", cookies}, {"User-Agent", user_agent}};
  updateDynamic(headers);
  updateLive(headers);
  updateGuard(headers);
}

void BuffManager::updateDynamic(const httplib::Headers& headers) {
  httplib::SSLClient dynamicCli("api.bilibili.com", 443);
  dynamicCli.set_connection_timeout(std::chrono::seconds(1));

  auto dres = dynamicCli.Get(
      "/x/polymer/web-dynamic/v1/feed/space?host_mid=61639371", headers);
  if (dres && dres->status == 200) {
    try {
      time_t new_latest_ = 0;
      auto json = nlohmann::json::parse(dres->body);
      if (json["code"].get<int>() != 0) {
        LAppPal::PrintLog(LogLevel::Warn, "[BuffManager]Fetch dynamic failed code %d", json["code"].get<int>());
        return;
      }
      auto dynamic_items = json["data"]["items"];
      if (dynamic_items.size() > 0) {
        new_latest_ = dynamic_items[0]["modules"]["module_author"]["pub_ts"];
        // in case there is a topmost dynamic
        if (dynamic_items.size() > 1) {
          new_latest_ = std::max(
              new_latest_, dynamic_items[1]["modules"]["module_author"]["pub_ts"]
                           .get<time_t>());
        }
      }
      if (new_latest_ > latest_) {
        latest_ = new_latest_;
        LAppPal::PrintLog(LogLevel::Info, "[BuffManager]Latest dynamic posted: %d", latest_);
      }
      time_t current = time(NULL);
      if (current - latest_ <= 4 * 60 * 60) {
        is_dynamic_ = true;
      }
    } catch(const std::exception& e) {
      is_dynamic_ = false;
      LAppPal::PrintLog(LogLevel::Error, "[BuffManager]Parse dynamic response failed");
    }
  } else {
    is_dynamic_ = false;
  }
}

void BuffManager::updateLive(const httplib::Headers &headers) {
  httplib::SSLClient live_cli("api.live.bilibili.com", 443);
  live_cli.set_connection_timeout(std::chrono::seconds(1));

  nlohmann::json Params;
  Params["mid"] = 61639371;

  auto infores = live_cli.Get(
      ("/room/v1/Room/getRoomInfoOld?" + Wbi::Json_to_url_encode_str(Params))
          .c_str(),
      headers);
  if (infores && infores->status == 200) {
    try {
      auto json = nlohmann::json::parse(infores->body);
      int code = json["code"].get<int>();
      if (code != 0) {
        LAppPal::PrintLog(LogLevel::Warn,
                          "[BuffManager]Fetch room status failed %d", code);
        is_live_ = false;
        return;
      }
      is_live_ = json.at("data").at("liveStatus").get<int>() == 1;
    } catch (const std::exception &e) {
      is_live_ = false;
      LAppPal::PrintLog(LogLevel::Error, "[BuffManager]Parse room info failed %s", e.what());
    }
  } else {
    is_live_ = false;
    LAppPal::PrintLog(LogLevel::Error, "[BuffManager]Fetch room info failed");
  }
}

void BuffManager::updateGuard(const httplib::Headers& headers) {
  httplib::SSLClient guard_cli("api.live.bilibili.com", 443);
  guard_cli.set_connection_timeout(std::chrono::seconds(1));

  string uid = DataManager::GetInstance()->GetWithDefault("uid", "");
  if (uid.empty()) {
    string cookies = DataManager::GetInstance()->GetWithDefault("cookies", "");
    // get uid from cookies string, find DedeUserID
    std::regex pattern("DedeUserID=([0-9]+)");
    std::smatch match;
    std::regex_search(cookies, match, pattern);
    if (match.size() < 2) {
      LAppPal::PrintLog(LogLevel::Warn, "[BuffManager]No valid uid");
      return;
    }
    uid = match[1];
    DataManager::GetInstance()->SetRaw("uid", uid);
  }
  
  nlohmann::json Params;
  Params["target_id"] = uid;

  auto infores = guard_cli.Get(
      ("/xlive/web-ucenter/user/MedalWall?" + Wbi::Json_to_url_encode_str(Params))
          .c_str(),
      headers);
  if (infores && infores->status == 200) {
    try {
      auto json = nlohmann::json::parse(infores->body);
      int code = json["code"].get<int>();
      if (code != 0) {
        LAppPal::PrintLog(LogLevel::Warn,
                          "[BuffManager]Fetch medal failed %d", code);
        is_guard_ = false;
        return;
      }
      for (const auto& entry : json["data"]["list"]) {
        if (entry["medal_info"]["target_id"].get<long long>() != 61639371) {
          continue;
        }
        medal_level_ = entry["uinfo_medal"]["level"].get<int>();
        if (entry["uinfo_medal"]["guard_level"].get<int>() > 0) {
          is_guard_ = true;
        }
      }
    } catch(const std::exception& e) {
      is_guard_ = false;
      LAppPal::PrintLog(LogLevel::Error, "[BuffManager]Parse medal info failed %s", e.what());
    }
  } else {
    is_guard_ = false;
    LAppPal::PrintLog(LogLevel::Error, "[BuffManager]Fetch medal info failed");
  }
}
