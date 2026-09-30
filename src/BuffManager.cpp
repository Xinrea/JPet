#include "BuffManager.hpp"
#include "DataManager.hpp"
#include "LAppPal.hpp"
#include "PanelServer.hpp"
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
  auto start_time = std::chrono::high_resolution_clock::now();
  DataManager* dm = DataManager::GetInstance();
  auto cookies = dm->GetWithDefault("cookies", "");
  auto user_agent = dm->GetWithDefault("user-agent", "");
  if (cookies.empty()) {
    // not login, clear account-dependent buffs
    is_live_ = false;
    is_dynamic_ = false;
    is_guard_ = false;
    medal_level_ = 0;
    latest_dynamic_ = 0;
    dynamic_retry_at_ = 0;
    wbi_config_.reset();
    PanelServer::GetInstance()->Notify("UPDATE");
    return;
  }
  httplib::Headers headers = {{"cookie", cookies}, {"User-Agent", user_agent}};
  updateDynamic(headers);
  updateLive(headers);
  updateGuard(headers);
  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
  if (duration.count() >= 1500) {
    LAppPal::PrintLog(LogLevel::Info,
                      "[BuffManager]Update buffs status cost=%dms",
                      duration.count());
  }
  PanelServer::GetInstance()->Notify("UPDATE");
}

void BuffManager::updateDynamic(const httplib::Headers& headers) {
  const time_t now = time(nullptr);
  is_dynamic_ =
      latest_dynamic_ > 0 && latest_dynamic_ <= now &&
      now - latest_dynamic_ <= 4 * 60 * 60;
  if (now < dynamic_retry_at_ || BilibiliDynamic::IsRateLimited(now)) {
    return;
  }

  httplib::SSLClient dynamic_cli("api.bilibili.com", 443);
  dynamic_cli.set_connection_timeout(std::chrono::seconds(1));
  dynamic_cli.set_read_timeout(std::chrono::seconds(5));
  dynamic_cli.set_write_timeout(std::chrono::seconds(5));
  const auto schedule_retry = [&](int seconds) {
    dynamic_retry_at_ = now + seconds;
  };

  const auto dynamic_headers =
      BilibiliDynamic::BuildHeaders(
          headers.find("cookie") == headers.end()
              ? ""
              : headers.find("cookie")->second,
          headers.find("User-Agent") == headers.end()
              ? ""
              : headers.find("User-Agent")->second,
          "61639371",
          BilibiliDynamic::FetchBuvid3(
              headers.find("User-Agent") == headers.end()
                  ? ""
                  : headers.find("User-Agent")->second));
  try {
    if (!wbi_config_) {
      wbi_config_ = BilibiliDynamic::FetchWbiConfig(
          headers.find("cookie") == headers.end()
              ? ""
              : headers.find("cookie")->second,
          headers.find("User-Agent") == headers.end()
              ? ""
              : headers.find("User-Agent")->second);
    }
  } catch (const std::exception& e) {
    schedule_retry(120);
    LAppPal::PrintLog(LogLevel::Warn,
                      "[BuffManager]Fetch dynamic WBI key failed %s", e.what());
    return;
  }
  std::string request_path;
  try {
    request_path =
        BilibiliDynamic::BuildSignedFeedPath("61639371", wbi_config_);
  } catch (const std::exception& e) {
    schedule_retry(120);
    LAppPal::PrintLog(LogLevel::Warn,
                      "[BuffManager]Build dynamic request failed %s", e.what());
    return;
  }
  int response_status = 0;
  std::string response_body;
  auto response = dynamic_cli.Get(request_path.c_str(), dynamic_headers);
  if (response) {
    response_status = response->status;
    response_body = response->body;
  } else {
    const auto fallback = cpr::Get(
        cpr::Url{"https://api.bilibili.com" + request_path},
        cpr::Header{{"Cookie", dynamic_headers.find("Cookie")->second},
                    {"User-Agent", dynamic_headers.find("User-Agent")->second},
                    {"Referer", dynamic_headers.find("Referer")->second},
                    {"Accept", "application/json, text/plain, */*"}},
        cpr::Timeout{5000});
    response_status = static_cast<int>(fallback.status_code);
    response_body = fallback.text;
  }
  if (response_status == 0) {
    schedule_retry(120);
    LAppPal::PrintLog(
        LogLevel::Warn,
        "[BuffManager]Fetch dynamic failed transport error=%s",
        httplib::to_string(response.error()).c_str());
    return;
  }
  if (response_status == 412) {
    const auto cooldown = BilibiliDynamic::MarkRateLimited(now);
    schedule_retry(cooldown);
    LAppPal::PrintLog(
        LogLevel::Warn,
        "[BuffManager]Dynamic API rate limited with HTTP 412, retry in %ds",
        cooldown);
    return;
  }
  if (response_status != 200) {
    schedule_retry(120);
    LAppPal::PrintLog("[BuffManager]Fetch dynamic failed HTTP %d",
                      response_status);
    return;
  }

  try {
    const auto json = nlohmann::json::parse(response_body);
    const int code = BilibiliDynamic::BusinessCode(json);
    if (code == 412 || code == -412) {
      const auto cooldown = BilibiliDynamic::MarkRateLimited(now);
      schedule_retry(cooldown);
      LAppPal::PrintLog(
          LogLevel::Warn,
          "[BuffManager]Dynamic API rate limited with code %d, retry in %ds",
          code, cooldown);
      return;
    }
    if (code != 0) {
      if (code == -403 || code == -352) {
        wbi_config_.reset();
      }
      schedule_retry(120);
      LAppPal::PrintLog(LogLevel::Warn,
                        "[BuffManager]Fetch dynamic failed code %d", code);
      return;
    }

    BilibiliDynamic::MarkSuccess();
    const auto* items = BilibiliDynamic::Items(json);
    if (items == nullptr) {
      schedule_retry(120);
      LAppPal::PrintLog(
          LogLevel::Warn,
          "[BuffManager]Dynamic response has no data.items array");
      return;
    }

    schedule_retry(120);
    for (const auto& item : *items) {
      if (BilibiliDynamic::IsLive(item)) {
        continue;
      }
      latest_dynamic_ =
          std::max(latest_dynamic_, BilibiliDynamic::ExtractPublishedAt(item));
    }
    is_dynamic_ =
        latest_dynamic_ > 0 && latest_dynamic_ <= now &&
        now - latest_dynamic_ <= 4 * 60 * 60;
  } catch (const std::exception& e) {
    schedule_retry(120);
    LAppPal::PrintLog(LogLevel::Warn,
                      "[BuffManager]Parse dynamic response failed %s",
                      e.what());
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

std::vector<std::string> BuffManager::GetBuffList() {
  std::vector<std::string> buffs;
  if (is_live_) {
    buffs.push_back("live");
  }
  if (is_dynamic_) {
    buffs.push_back("dynamic");
  }
  if (is_guard_) {
    buffs.push_back("guard");
  }
  if (IsFail()) {
    buffs.push_back("fail");
  }
  if (IsMonday()) {
    buffs.push_back("monday");
  }
  if (IsBirthday()) {
    buffs.push_back("birthday");
  }
  return buffs;
}

bool BuffManager::IsFail() {
  return DataManager::GetInstance()->GetWithDefault("buff.failcount", 0) >= 2;
}

bool BuffManager::IsMonday() {
  time_t now = time(0);
  tm ltm;
#ifdef _WIN32
  localtime_s(&ltm, &now);
#else
  localtime_r(&now, &ltm);
#endif
  return ltm.tm_wday == 1;
}

bool BuffManager::IsBirthday() {
  time_t now = time(0);
  tm ltm;
#ifdef _WIN32
  localtime_s(&ltm, &now);
#else
  localtime_r(&now, &ltm);
#endif
  return ltm.tm_mon == 10 && ltm.tm_mday == 25;
}
