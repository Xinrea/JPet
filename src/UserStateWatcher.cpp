#include "UserStateWatcher.h"
#include "LAppPal.hpp"
#include "PanelServer.hpp"
#include "Wbi.hpp"

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <utility>

UserStateWatcher::UserStateWatcher(const string& uid,
                                   const string& userAgent, shared_ptr<WbiConfig> wbi_config)
    : _userAgent(userAgent), _wbi_config(wbi_config) {
  target.uid = uid;
}

void UserStateWatcher::initBasicInfo(const string& cookies) {
  const auto headers = BilibiliDynamic::BuildHeaders(
      cookies, _userAgent, target.uid, BilibiliDynamic::FetchBuvid3(_userAgent));

  httplib::SSLClient client = httplib::SSLClient("api.bilibili.com", 443);
  client.set_connection_timeout(std::chrono::seconds(1));
  client.set_read_timeout(std::chrono::seconds(5));
  client.set_write_timeout(std::chrono::seconds(5));

  string request_path = "/x/web-interface/card?";
  nlohmann::json Params;
  Params["mid"] = target.uid;
  request_path += Wbi::Json_to_url_encode_str(Params);

  const auto processBody = [&](const std::string& body) {
    try {
      auto json = nlohmann::json::parse(body);
      const int code = json.value("code", -1);
      if (code == -404) {
        target.uname = target.uid;
        _initialized = true;
        PanelServer::GetInstance()->Notify("NOTIFY_UPDATE");
        return true;
      }
      if (code != 0 || !json.at("data").is_object()) {
        return false;
      }
      const auto& card = json.at("data").at("card");
      if (!card.is_object()) return false;
      target.uname = card.value("name", target.uid);
      _initialized = true;
      PanelServer::GetInstance()->Notify("NOTIFY_UPDATE");
      return true;
    } catch (const std::exception&) {
      return false;
    }
  };

  auto res = client.Get(request_path.c_str(), headers);
  if (res && res->status == 200) {
    if (!processBody(res->body)) {
      try {
        const auto json = nlohmann::json::parse(res->body);
        const int code = json.value("code", -1);
        LAppPal::PrintLog(
            "[UserStateWatcher][%s]BasicInfo failed code=%d",
            target.uid.c_str(), code);
      } catch (const std::exception& e) {
        LAppPal::PrintLog(
            "[UserStateWatcher][%s]BasicInfo response parse failed %s",
            target.uid.c_str(), e.what());
      }
    }
  } else if (res) {
    LAppPal::PrintLog(
        "[UserStateWatcher][%s]BasicInfo HTTP failed status=%d",
        target.uid.c_str(), res->status);
  } else {
    LAppPal::PrintLog(
        "[UserStateWatcher][%s]BasicInfo transport failed error=%s",
        target.uid.c_str(), httplib::to_string(res.error()).c_str());
    // cpp-httplib can fail while reading a compressed response on some
    // Bilibili CDN paths. Retry the same public endpoint through cpr, which
    // uses libcurl's response decoding.
    const auto fallback = cpr::Get(
        cpr::Url{"https://api.bilibili.com" + request_path},
        cpr::Header{{"Cookie", cookies},
                    {"User-Agent", _userAgent},
                    {"Referer", "https://space.bilibili.com/" + target.uid +
                                    "/dynamic"}},
        cpr::Timeout{5000});
    if (fallback.status_code == 200 && processBody(fallback.text)) {
      return;
    }
    LAppPal::PrintLog("[UserStateWatcher][%s]BasicInfo fallback failed HTTP %d",
                      target.uid.c_str(), fallback.status_code);
  }
}

void UserStateWatcher::checkDynamic(queue<StateMessage>& messageQueue,
                                    const string& cookies) {
  const time_t now = time(nullptr);
  if (cookies.empty()) {
    dynamic_initialized = false;
    dynamic_ids.clear();
    dynamic_latest_time = 0;
    return;
  }
  if (BilibiliDynamic::IsRateLimited(now)) {
    return;
  }
  if (dynamic_next_check_at == 0) {
    dynamic_next_check_at =
        now + 180 + BilibiliDynamic::RequestJitter(target.uid);
    return;
  }
  if (now < dynamic_next_check_at || now < dynamic_retry_at) {
    return;
  }

  httplib::SSLClient dynamic_cli("api.bilibili.com", 443);
  dynamic_cli.set_connection_timeout(std::chrono::seconds(1));
  dynamic_cli.set_read_timeout(std::chrono::seconds(5));
  dynamic_cli.set_write_timeout(std::chrono::seconds(5));
  const auto headers =
      BilibiliDynamic::BuildHeaders(
          cookies, _userAgent, target.uid,
          BilibiliDynamic::FetchBuvid3(_userAgent));
  if (!_wbi_config && now >= wbi_retry_at) {
    try {
      _wbi_config = BilibiliDynamic::FetchWbiConfig(cookies, _userAgent);
      wbi_retry_at = now + (_wbi_config ? 6 * 60 * 60 : 60);
    } catch (const std::exception& e) {
      dynamic_next_check_at = now + 120;
      LAppPal::PrintLog("[UserStateWatcher][%s]Fetch dynamic WBI key failed %s",
                        target.uid.c_str(), e.what());
      return;
    }
  }
  std::string request_path;
  try {
    request_path =
        BilibiliDynamic::BuildSignedFeedPath(target.uid, _wbi_config);
  } catch (const std::exception& e) {
    dynamic_next_check_at = now + 120;
    LAppPal::PrintLog("[UserStateWatcher][%s]Build dynamic request failed %s",
                      target.uid.c_str(), e.what());
    return;
  }
  int response_status = 0;
  std::string response_body;
  auto response = dynamic_cli.Get(request_path.c_str(), headers);
  if (response) {
    response_status = response->status;
    response_body = response->body;
  } else {
    const auto fallback = cpr::Get(
        cpr::Url{"https://api.bilibili.com" + request_path},
        cpr::Header{{"Cookie", headers.find("Cookie")->second},
                    {"User-Agent", headers.find("User-Agent")->second},
                    {"Referer", headers.find("Referer")->second},
                    {"Accept", "application/json, text/plain, */*"}},
        cpr::Timeout{5000});
    response_status = static_cast<int>(fallback.status_code);
    response_body = fallback.text;
  }
  if (response_status == 0) {
    dynamic_next_check_at = now + 120;
    LAppPal::PrintLog("[UserStateWatcher][%s]Fetch dynamic failed",
                      target.uid.c_str());
    return;
  }

  const auto schedule_retry = [&]() {
    dynamic_retry_at = now + 120;
    dynamic_next_check_at = dynamic_retry_at;
  };
  if (response_status == 412) {
    BilibiliDynamic::MarkRateLimited(now);
    schedule_retry();
    LAppPal::PrintLog(
        "[UserStateWatcher][%s]Dynamic API rate limited with HTTP 412",
        target.uid.c_str());
    return;
  }
  if (response_status != 200) {
    dynamic_retry_at = now + 120;
    LAppPal::PrintLog("[UserStateWatcher][%s]Fetch dynamic failed HTTP %d",
                      target.uid.c_str(), response_status);
    return;
  }

  try {
    const auto json = nlohmann::json::parse(response_body);
    const int code = BilibiliDynamic::BusinessCode(json);
    if (code == 412 || code == -412) {
      BilibiliDynamic::MarkRateLimited(now);
      schedule_retry();
      LAppPal::PrintLog(
          "[UserStateWatcher][%s]Dynamic API rate limited with code %d",
          target.uid.c_str(), code);
      return;
    }
    if (code != 0) {
      if (code == -403 || code == -352) {
        _wbi_config.reset();
        wbi_retry_at = 0;
      }
      dynamic_next_check_at = now + 120;
      LAppPal::PrintLog("[UserStateWatcher][%s]Fetch dynamic failed code %d",
                        target.uid.c_str(), code);
      return;
    }

    BilibiliDynamic::MarkSuccess();
    const auto* items = BilibiliDynamic::Items(json);
    if (items == nullptr) {
      dynamic_next_check_at = now + 120;
      LAppPal::PrintLog(
          "[UserStateWatcher][%s]Dynamic response has no data.items array",
          target.uid.c_str());
      return;
    }

    dynamic_retry_at = 0;
    dynamic_next_check_at =
        now + 120 + BilibiliDynamic::RequestJitter(target.uid);
    std::vector<std::string> latest_ids;
    latest_ids.reserve(std::min(items->size(), size_t{20}));
    for (const auto& item : *items) {
      if (BilibiliDynamic::IsLive(item)) {
        continue;
      }
      const auto id = BilibiliDynamic::ExtractDynamicId(item);
      if (id.empty() ||
          std::find(latest_ids.begin(), latest_ids.end(), id) !=
              latest_ids.end()) {
        continue;
      }
      latest_ids.push_back(id);
    }
    if (latest_ids.empty()) {
      if (!dynamic_initialized) {
        dynamic_initialized = true;
      }
      return;
    }
    if (!dynamic_initialized) {
      dynamic_ids = std::move(latest_ids);
      for (const auto& item : *items) {
        if (BilibiliDynamic::IsLive(item)) continue;
        dynamic_latest_time =
            std::max(dynamic_latest_time,
                     BilibiliDynamic::ExtractPublishedAt(item));
      }
      if (dynamic_ids.size() > 128) {
        dynamic_ids.resize(128);
      }
      dynamic_initialized = true;
      return;
    }

    const auto previous_ids = dynamic_ids;

    std::vector<std::pair<std::string, std::string>> new_notifications;
    for (const auto& item : *items) {
      if (new_notifications.size() >= 5) {
        break;
      }
      const auto id = BilibiliDynamic::ExtractDynamicId(item);
      if (id.empty() ||
          std::find(previous_ids.begin(), previous_ids.end(), id) !=
              previous_ids.end() ||
          BilibiliDynamic::IsLive(item)) {
        continue;
      }
      const auto published_at = BilibiliDynamic::ExtractPublishedAt(item);
      if (published_at > 0 && dynamic_latest_time > 0 &&
          published_at < dynamic_latest_time) {
        continue;
      }
      auto text = BilibiliDynamic::ExtractDynamicText(item);
      if (text.empty()) {
        text = "动态有更新";
      }
      new_notifications.emplace_back(id, text);
    }
    for (const auto& id : latest_ids) {
      if (std::find(dynamic_ids.begin(), dynamic_ids.end(), id) ==
          dynamic_ids.end()) {
        dynamic_ids.push_back(id);
      }
    }
    for (const auto& item : *items) {
      if (BilibiliDynamic::IsLive(item)) continue;
      dynamic_latest_time =
          std::max(dynamic_latest_time,
                   BilibiliDynamic::ExtractPublishedAt(item));
    }
    while (dynamic_ids.size() > 128) {
      dynamic_ids.erase(dynamic_ids.begin());
    }
    for (auto it = new_notifications.rbegin(); it != new_notifications.rend();
         ++it) {
      messageQueue.push(
          StateMessage(MessageType::DynamicMessage, target, it->first, it->second));
    }
  } catch (const std::exception& e) {
    dynamic_next_check_at = now + 120;
    LAppPal::PrintLog("[UserStateWatcher][%s]Parse dynamic failed %s",
                      target.uid.c_str(), e.what());
  }
}

CheckStatus UserStateWatcher::Check(queue<StateMessage>& messageQueue, const string& cookies) {
  const auto now = time(nullptr);
  if (!_wbi_config && !cookies.empty() && now >= wbi_retry_at) {
    _wbi_config = BilibiliDynamic::FetchWbiConfig(cookies, _userAgent);
    wbi_retry_at = now + (_wbi_config ? 6 * 60 * 60 : 60);
  }
  if (!_initialized) {
    initBasicInfo(cookies);
  }

  if (!_initialized) {
    return CheckStatus::FAST;
  }

  checkDynamic(messageQueue, cookies);

  const auto headers = BilibiliDynamic::BuildHeaders(
      cookies, _userAgent, target.uid, BilibiliDynamic::FetchBuvid3(_userAgent));

  httplib::SSLClient liveCli("api.live.bilibili.com", 443);
  liveCli.set_connection_timeout(std::chrono::seconds(1));
  liveCli.set_read_timeout(std::chrono::seconds(5));
  liveCli.set_write_timeout(std::chrono::seconds(5));

  nlohmann::json payload;
  try {
    payload["uids"] = nlohmann::json::array({std::stoll(target.uid)});
  } catch (const std::exception&) {
    return CheckStatus::SUCCESS;
  }
  auto infores = liveCli.Post("/room/v1/Room/get_status_info_by_uids",
                              headers, payload.dump(), "application/json");
  if (infores && infores->status == 200) {
    try {
      auto json = nlohmann::json::parse(infores->body);
      if (json.at("code").get<int>() == 0 && json.at("data").is_object()) {
        const auto status_it = json.at("data").find(target.uid);
        if (status_it == json.at("data").end() || !status_it->is_object()) {
          target.roomid.clear();
          lastStatus = false;
          return CheckStatus::SUCCESS;
        }
        const auto& status_info = *status_it;
        const bool status =
            status_info.value("live_status", 0) == 1;
        target.roomid =
            std::to_string(status_info.value("room_id", 0LL));
        if (target.roomid == "0") target.roomid.clear();
        target.roomtitle = status_info.value("title", "");
        const auto reported_name = status_info.value("uname", "");
        if (!reported_name.empty()) target.uname = reported_name;
        if (!lastStatus && status) {
          messageQueue.push(StateMessage(MessageType::LiveMessage, target));
        }
        lastStatus = status;
      }
    } catch (const std::exception& e) {
      LAppPal::PrintLog("[UserStateWatcher]Parse room info failed %s",
                        e.what());
    }
  } else {
    LAppPal::PrintLog(LogLevel::Debug,
                      "[UserStateWatcher]Fetch room info failed");
  }
  return CheckStatus::SUCCESS;
}
