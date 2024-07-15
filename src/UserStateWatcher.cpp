#include "UserStateWatcher.h"
#include "Wbi.hpp"

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <iostream>

#include "LAppDefine.hpp"
using namespace LAppDefine;

UserStateWatcher::UserStateWatcher(const string& uid, const string& cookies,
                                   const string& userAgent)
    : _cookies(cookies), _userAgent(userAgent) {
  target.uid = uid;

  auto p = Wbi::Get_wbi_key();
  img_key = p.first;
  sub_key = p.second;

  initBasicInfo();
}

void UserStateWatcher::initBasicInfo() {
  httplib::Headers headers = {{"cookie", _cookies}, {"User-Agent", _userAgent}};

  // change client host
  httplib::SSLClient client = httplib::SSLClient("api.bilibili.com", 443);
  client.set_connection_timeout(std::chrono::seconds(1));

  nlohmann::json Params;
  Params["mid"] = target.uid;
  const auto mixin_key = Wbi::Get_mixin_key(img_key, sub_key);
  const auto w_rid = Wbi::Calc_sign(Params, mixin_key);

  auto res =
      client.Get(("/x/space/acc/info?" + Wbi::Json_to_url_encode_str(Params) +
                  "&w_rid=" + w_rid)
                     .c_str(),
                 headers);
  if (res && res->status == 200) {
    try {
      auto json = nlohmann::json::parse(res->body);
      if (json.at("code") == 0 && json.contains("data")) {
        target.uname = json.at("data").at("name").get<std::string>();
        // user may not have a room id
        if (json.at("data").contains("live_room")) {
          target.roomtitle =
              json.at("data").at("live_room").at("title").get<std::string>();
          target.roomid = std::to_string(
              json.at("data").at("live_room").at("roomid").get<int>());
        }
        _initialized = true;
      } else {
        LAppPal::PrintLog("[UserStateWatcher][%s]BasicInfo Failed with code %d",
                          target.uid.c_str(), json.at("code").get<int>());
      }
    } catch (const std::exception& e) {
      LAppPal::PrintLog("[UserStateWatcher][%s]BasicInfo Failed %s",
                        target.uid.c_str(), e.what());
    }
  } else {
    LAppPal::PrintLog("[UserStateWatcher][%s]BasicInfo Failed",
                      target.uid.c_str());
  }
}

std::wstring UserStateWatcher::StringToWString(const std::string& str) {
  int num = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
  wchar_t* wide = new wchar_t[num];
  MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, wide, num);
  std::wstring w_str(wide);
  delete[] wide;
  return w_str;
}

bool UserStateWatcher::Check(queue<StateMessage>& messageQueue) {
  if (!_initialized) {
    initBasicInfo();
  }

  if (!_initialized) {
    return true;
  }

  httplib::Headers headers = {{"cookie", _cookies}, {"User-Agent", _userAgent}};

  httplib::SSLClient dynamicCli("api.bilibili.com", 443);
  dynamicCli.set_connection_timeout(std::chrono::seconds(1));

  auto dres = dynamicCli.Get(
      ("/x/polymer/web-dynamic/v1/feed/space?host_mid=" + target.uid).c_str(),
      headers);
  if (dres && dres->status == 200) {
    try {
      auto json = nlohmann::json::parse(dres->body);
      if (json.at("code").get<int>() == 0 && json.contains("data")) {
        // get total items number
        int total = json.at("data").at("items").size();
        int validIndex = -1;
        // skip all live card
        for (int i = 0; i < total; i++) {
          if (json.at("data").at("items").at(i).at("type").get<string>() !=
              "DYNAMIC_TYPE_LIVE_RCMD") {
            validIndex = i;
            break;
          }
        }
        if (validIndex == 0 &&
            json.at("data").at("items").at(0).at("modules").contains(
                "module_tag")) {
          // find another valid index
          for (int i = 1; i < total; i++) {
            if (json.at("data").at("items").at(i).at("type").get<string>() !=
                "DYNAMIC_TYPE_LIVE_RCMD") {
              validIndex = i;
              break;
            }
          }
        }
        // data.items[i].modules.module_author.pub_ts
        long long latest = json.at("data")
                               .at("items")
                               .at(0)
                               .at("modules")
                               .at("module_author")
                               .at("pub_ts")
                               .get<long long>();
        // valid might be the top, so we need to check the second one
        if (total > 1) {
          latest = max(latest, json.at("data")
                                   .at("items")
                                   .at(1)
                                   .at("modules")
                                   .at("module_author")
                                   .at("pub_ts")
                                   .get<long long>());
        }
        if (validIndex == -1) {
          LAppPal::PrintLog(LogLevel::Error, "[UserStateWatcher][%s]No valid dynamic",
                            target.uid.c_str());
          lastTime = latest;
          return true;
        }
        string dynamic_id = json.at("data")
                                .at("items")
                                .at(validIndex)
                                .at("id_str")
                                .get<string>();
        string dynamic_type =
            json.at("data").at("items").at(validIndex).at("type").get<string>();
        string dynamic_desc;
        if (dynamic_type == "DYNAMIC_TYPE_AV") {
          dynamic_desc = json.at("data")
                             .at("items")
                             .at(validIndex)
                             .at("modules")
                             .at("module_dynamic")
                             .at("major")
                             .at("archive")
                             .at("title")
                             .get<string>();
        } else {
          // data.items[0].modules.module_dynamic.desc.text
          dynamic_desc = json.at("data")
                             .at("items")
                             .at(validIndex)
                             .at("modules")
                             .at("module_dynamic")
                             .at("desc")
                             .at("text")
                             .get<string>();
        }
        // if (lastTime != 0) {
        if (latest > lastTime) {
          messageQueue.push(StateMessage(MessageType::DynamicMessage, target,
                                         dynamic_id, dynamic_desc));
        }
        // }
        lastTime = latest;
      } else {
        LAppPal::PrintLog("[UserStateWatcher][%s]Fetch dynamic failed %d",
                          target.uid.c_str(), json.at("code").get<int>());
        // if code is -352
        if (json.at("code").get<int>() == -352) {
          return false;
        }
      }
    } catch (const std::exception& e) {
      LAppPal::PrintLog("[UserStateWatcher][%s]Parse dynamic failed %s",
                        target.uid.c_str(), e.what());
    }
  } else {
    LAppPal::PrintLog("[UserStateWatcher][%s]Fetch Dynamic Failed: %s",
                      target.uid.c_str(), dres->body.c_str());
  }

  if (target.roomid.empty()) {
    LAppPal::PrintLog("[UserStateWatcher][%s]No room id", target.uid.c_str());
    return true;
  }
  httplib::SSLClient liveCli("api.live.bilibili.com", 443);
  liveCli.set_connection_timeout(std::chrono::seconds(1));

  // https://api.live.bilibili.com/room/v1/Room/getRoomInfoOld?mid=475210
  nlohmann::json Params;
  Params["mid"] = target.uid;
  const auto mixin_key = Wbi::Get_mixin_key(img_key, sub_key);
  const auto w_rid = Wbi::Calc_sign(Params, mixin_key);

  auto infores =
      liveCli.Get(("/room/v1/Room/getRoomInfoOld?" +
                   Wbi::Json_to_url_encode_str(Params) + "&w_rid=" + w_rid)
                      .c_str(),
                  headers);
  if (infores && infores->status == 200) {
    try {
      auto json = nlohmann::json::parse(infores->body);
      if (json.at("code").get<int>() == 0 && json.contains("data")) {
        bool status = json.at("data").at("liveStatus").get<int>() == 1;
        target.roomtitle = json.at("data").at("title").get<std::string>();
        if (!lastStatus && status) {
          messageQueue.push(
              StateMessage(MessageType::LiveMessage, target, "", ""));
        }
        lastStatus = status;
      }
    } catch (const std::exception& e) {
      LAppPal::PrintLog("[UserStateWatcher]Parse room info failed %s",
                        e.what());
    }
  } else {
    if (DebugLogEnable) {
      LAppPal::PrintLog("[UserStateWatcher]Fetch room info failed");
    }
  }
  return true;
}
