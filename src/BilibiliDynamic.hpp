#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <cpr/cpr.h>

#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1
#include <cryptopp/filters.h>
#include <cryptopp/hex.h>
#include <cryptopp/md5.h>

#include "Wbi.hpp"

namespace BilibiliDynamic {

// Request/risk parameters follow the MIT-licensed
// Mooling0602/bilibili-feed-apis implementation at:
// https://github.com/Mooling0602/bilibili-feed-apis/tree/31c86a1d43eb67798e77a3de8dcbce4915f394e5
// Text and nested-content fallbacks are independently implemented from the
// response shapes documented by:
// https://github.com/jiudaimu/astrbot_plugin_bili_notify_plus
inline constexpr char kFeedPath[] =
    "/x/polymer/web-dynamic/v1/feed/space";
inline constexpr char kFeatures[] =
    "itemOpusStyle,listOnlyfans,opusBigCover,onlyfansVote,decorationCard,"
    "onlyfansAssetsV2,forwardListDecoration,ugcDelete,onlyfansQa498";
inline constexpr char kDefaultUserAgent[] =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36";

inline int RequestJitter(std::string_view uid) {
  return 2 + static_cast<int>(std::hash<std::string_view>{}(uid) % 5);
}

inline std::mutex& RiskMutex() {
  static std::mutex mutex;
  return mutex;
}

inline time_t& RiskRetryAt() {
  static time_t retry_at = 0;
  return retry_at;
}

inline int& RiskFailures() {
  static int failures = 0;
  return failures;
}

inline bool IsRateLimited(time_t now = time(nullptr)) {
  std::lock_guard<std::mutex> lock(RiskMutex());
  return now < RiskRetryAt();
}

inline int MarkRateLimited(time_t now = time(nullptr)) {
  std::lock_guard<std::mutex> lock(RiskMutex());
  ++RiskFailures();
  const int cooldown = RiskFailures() == 1
                           ? 180
                           : RiskFailures() == 2 ? 600 : 1800;
  RiskRetryAt() = now + cooldown;
  return cooldown;
}

inline void MarkSuccess() {
  std::lock_guard<std::mutex> lock(RiskMutex());
  if (time(nullptr) < RiskRetryAt()) return;
  RiskFailures() = 0;
  RiskRetryAt() = 0;
}

inline const nlohmann::json* AnyField(const nlohmann::json& object,
                                      const char* key) {
  if (!object.is_object()) return nullptr;
  const auto it = object.find(key);
  return it == object.end() ? nullptr : &(*it);
}

inline const nlohmann::json* ObjectField(const nlohmann::json& object,
                                         const char* key) {
  const auto* value = AnyField(object, key);
  return value != nullptr && value->is_object() ? value : nullptr;
}

inline std::string StringValue(const nlohmann::json* value) {
  if (value == nullptr) return {};
  if (value->is_string()) return value->get<std::string>();
  if (value->is_number_integer() || value->is_number_unsigned()) {
    return value->dump();
  }
  return {};
}

inline int BusinessCode(const nlohmann::json& response) {
  const auto* code = AnyField(response, "code");
  return code != nullptr && code->is_number_integer() ? code->get<int>() : -1;
}

inline const nlohmann::json* Items(const nlohmann::json& response) {
  const auto* data = ObjectField(response, "data");
  const auto* items = data == nullptr ? nullptr : AnyField(*data, "items");
  return items != nullptr && items->is_array() ? items : nullptr;
}

inline std::string Base64Encode(std::string_view value) {
  static constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  unsigned int buffer = 0;
  int bits = 0;
  for (const unsigned char c : value) {
    buffer = (buffer << 8) | c;
    bits += 8;
    while (bits >= 6) {
      bits -= 6;
      result.push_back(alphabet[(buffer >> bits) & 0x3f]);
    }
  }
  if (bits > 0) result.push_back(alphabet[(buffer << (6 - bits)) & 0x3f]);
  while (result.size() % 4 != 0) result.push_back('=');
  return result;
}

inline std::string RandomDmImageString() {
  static std::mutex mutex;
  static std::mt19937 generator(std::random_device{}());
  std::lock_guard<std::mutex> lock(mutex);
  std::uniform_int_distribution<int> length_distribution(32, 128);
  std::uniform_int_distribution<int> character_distribution(33, 126);
  const int length = length_distribution(generator);
  std::string value;
  value.reserve(length);
  for (int i = 0; i < length; ++i) {
    value.push_back(static_cast<char>(character_distribution(generator)));
  }
  auto result = Base64Encode(value);
  if (result.size() >= 2) result.resize(result.size() - 2);
  return result;
}

inline std::string Md5Hex(const std::string& value) {
  CryptoPP::Weak1::MD5 hash;
  std::string result;
  CryptoPP::StringSource source(
      value, true,
      new CryptoPP::HashFilter(
          hash, new CryptoPP::HexEncoder(new CryptoPP::StringSink(result))));
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

inline httplib::Headers BuildHeaders(const std::string& cookies,
                                     const std::string& user_agent,
                                     const std::string& uid,
                                     const std::string& buvid3 = {});

inline std::string FetchBuvid3(const std::string& user_agent) {
  static std::mutex mutex;
  static std::string cached;
  static time_t expires_at = 0;
  const auto now = time(nullptr);
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (!cached.empty() && now < expires_at) return cached;
  }
  httplib::SSLClient client("api.bilibili.com", 443);
  client.set_connection_timeout(std::chrono::seconds(1));
  client.set_read_timeout(std::chrono::seconds(5));
  client.set_write_timeout(std::chrono::seconds(5));
  auto response = client.Get("/x/frontend/finger/spi",
                             BuildHeaders({}, user_agent, {}));
  int status = response ? response->status : 0;
  std::string body = response ? response->body : std::string{};
  if (!response) {
    const auto fallback = cpr::Get(
        cpr::Url{"https://api.bilibili.com/x/frontend/finger/spi"},
        cpr::Header{{"User-Agent", user_agent}}, cpr::Timeout{5000});
    status = static_cast<int>(fallback.status_code);
    body = fallback.text;
  }
  if (status != 200) return {};
  try {
    const auto json = nlohmann::json::parse(body);
    const auto* data = ObjectField(json, "data");
    const auto value = StringValue(data == nullptr ? nullptr
                                                    : AnyField(*data, "b_3"));
    if (BusinessCode(json) != 0 || value.empty()) return {};
    std::lock_guard<std::mutex> lock(mutex);
    cached = value;
    expires_at = now + 24 * 60 * 60;
    return cached;
  } catch (const std::exception&) {
    return {};
  }
}

inline std::shared_ptr<WbiConfig> FetchWbiConfig(const std::string& cookies,
                                                 const std::string& user_agent) {
  httplib::SSLClient client("api.bilibili.com", 443);
  client.set_connection_timeout(std::chrono::seconds(1));
  client.set_read_timeout(std::chrono::seconds(5));
  client.set_write_timeout(std::chrono::seconds(5));
  auto response = client.Get(
      "/x/web-interface/nav",
      BuildHeaders(cookies, user_agent, {}, FetchBuvid3(user_agent)));
  int status = response ? response->status : 0;
  std::string body = response ? response->body : std::string{};
  if (!response) {
    const auto fallback = cpr::Get(
        cpr::Url{"https://api.bilibili.com/x/web-interface/nav"},
        cpr::Header{{"Cookie", cookies},
                    {"User-Agent", user_agent},
                    {"Referer", "https://www.bilibili.com/"}},
        cpr::Timeout{5000});
    status = static_cast<int>(fallback.status_code);
    body = fallback.text;
  }
  if (status != 200) return nullptr;
  try {
    const auto json = nlohmann::json::parse(body);
    const auto* data = ObjectField(json, "data");
    const auto* wbi = data == nullptr ? nullptr : ObjectField(*data, "wbi_img");
    const auto key = [](const nlohmann::json* value) {
      const auto url = StringValue(value);
      const auto slash = url.find_last_of('/');
      const auto dot = url.find_last_of('.');
      return slash == std::string::npos || dot <= slash + 1
                 ? std::string{}
                 : url.substr(slash + 1, dot - slash - 1);
    };
    auto config = std::make_shared<WbiConfig>();
    config->img_key = key(wbi == nullptr ? nullptr : AnyField(*wbi, "img_url"));
    config->sub_key = key(wbi == nullptr ? nullptr : AnyField(*wbi, "sub_url"));
    return config->img_key.size() == 32 && config->sub_key.size() == 32
               ? config
               : nullptr;
  } catch (const std::exception&) {
    return nullptr;
  }
}

inline std::string SignQuery(nlohmann::json params,
                             const std::string& img_key,
                             const std::string& sub_key,
                             int64_t timestamp) {
  for (auto& [key, value] : params.items()) {
    if (!value.is_string()) continue;
    auto text = value.get<std::string>();
    text.erase(std::remove_if(text.begin(), text.end(), [](char c) {
                 return std::string_view("!'()*").find(c) !=
                        std::string_view::npos;
               }),
               text.end());
    value = std::move(text);
  }
  params["wts"] = timestamp;
  const auto query = Wbi::Json_to_url_encode_str(params) +
                     Wbi::Get_mixin_key(img_key, sub_key);
  return Wbi::Json_to_url_encode_str(params) + "&w_rid=" + Md5Hex(query);
}

inline std::string BuildSignedFeedPath(
    const std::string& uid, const std::shared_ptr<WbiConfig>& config) {
  nlohmann::json params = {{"features", kFeatures},
                           {"host_mid", uid},
                           {"platform", "web"},
                           {"timezone_offset", "-480"},
                           {"dm_img_list", "[]"},
                           {"dm_img_str", RandomDmImageString()},
                           {"dm_cover_img_str", RandomDmImageString()},
                           {"dm_img_inter",
                            R"({"ds":[],"wh":[6093,6631,31],"of":[430,760,380]})"}};
  if (!config) {
    return std::string(kFeedPath) + "?host_mid=" + uid +
           "&timezone_offset=-480&platform=web&features=" +
           cpr::util::urlEncode(kFeatures);
  }
  return std::string(kFeedPath) + "?" +
         SignQuery(std::move(params), config->img_key, config->sub_key,
                   std::time(nullptr));
}

inline httplib::Headers BuildHeaders(const std::string& cookies,
                                     const std::string& user_agent,
                                     const std::string& uid,
                                     const std::string& buvid3) {
  std::string cookie_header = cookies;
  if (!buvid3.empty() && cookie_header.find("buvid3=") == std::string::npos) {
    if (!cookie_header.empty()) cookie_header += "; ";
    cookie_header += "buvid3=" + buvid3;
  }
  return {{"Cookie", cookie_header},
          {"User-Agent", user_agent.empty() ? kDefaultUserAgent : user_agent},
          {"Referer", uid.empty() ? "https://www.bilibili.com/"
                                 : "https://space.bilibili.com/" + uid +
                                       "/dynamic"},
          {"Accept", "application/json, text/plain, */*"},
          {"Accept-Encoding", "gzip, deflate"},
          {"Accept-Language", "zh-CN,zh;q=0.9"},
          {"Origin", "https://www.bilibili.com"}};
}

inline std::string ExtractDynamicId(const nlohmann::json& item) {
  auto id = StringValue(AnyField(item, "id_str"));
  if (id.empty()) id = StringValue(AnyField(item, "id"));
  return id;
}

inline int64_t ExtractPublishedAt(const nlohmann::json& item) {
  const auto* modules = ObjectField(item, "modules");
  const auto* author =
      modules == nullptr ? nullptr : ObjectField(*modules, "module_author");
  const auto text =
      author == nullptr ? std::string{} : StringValue(AnyField(*author, "pub_ts"));
  if (text.empty()) return 0;
  try {
    const auto value = std::stoll(text);
    return value > 100000000000LL ? value / 1000 : value;
  } catch (const std::exception&) {
    return 0;
  }
}

inline bool IsLive(const nlohmann::json& item) {
  const auto type = StringValue(AnyField(item, "type"));
  const auto* modules = ObjectField(item, "modules");
  const auto* dynamic =
      modules == nullptr ? nullptr : ObjectField(*modules, "module_dynamic");
  const auto* major =
      dynamic == nullptr ? nullptr : ObjectField(*dynamic, "major");
  const auto major_type =
      major == nullptr ? std::string{} : StringValue(AnyField(*major, "type"));
  return type == "DYNAMIC_TYPE_LIVE" || type == "DYNAMIC_TYPE_LIVE_RCMD" ||
         major_type == "MAJOR_TYPE_LIVE" ||
         major_type == "MAJOR_TYPE_LIVE_RCMD";
}

inline std::string CleanText(std::string value) {
  for (auto& c : value) {
    if (c == '\r' || c == '\n' || c == '\t') c = ' ';
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front()))) {
    value.erase(value.begin());
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  return value;
}

inline std::string ExtractDynamicText(const nlohmann::json& item, int depth = 0) {
  if (depth > 2) return {};
  const auto* modules = ObjectField(item, "modules");
  const auto* dynamic =
      modules == nullptr ? nullptr : ObjectField(*modules, "module_dynamic");
  if (dynamic != nullptr) {
    const auto* desc = ObjectField(*dynamic, "desc");
    auto text = desc == nullptr
                    ? std::string{}
                    : CleanText(StringValue(AnyField(*desc, "text")));
    if (text.empty() && desc != nullptr) {
      const auto* nodes = AnyField(*desc, "rich_text_nodes");
      if (nodes != nullptr && nodes->is_array()) {
        for (const auto& node : *nodes) {
          auto node_text = StringValue(AnyField(node, "orig_text"));
          if (node_text.empty()) node_text = StringValue(AnyField(node, "text"));
          text += node_text;
        }
        text = CleanText(text);
      }
    }
    if (!text.empty()) return text;
    const auto* major = ObjectField(*dynamic, "major");
    if (major != nullptr) {
      for (const char* kind : {"opus", "archive", "article", "draw"}) {
        const auto* card = ObjectField(*major, kind);
        if (card == nullptr) continue;
        for (const char* key : {"title", "summary", "desc"}) {
          const auto* value = AnyField(*card, key);
          text = CleanText(StringValue(value));
          if (text.empty() && value != nullptr && value->is_object()) {
            text = CleanText(StringValue(AnyField(*value, "text")));
            if (text.empty()) {
              const auto* nodes = AnyField(*value, "rich_text_nodes");
              if (nodes != nullptr && nodes->is_array()) {
                for (const auto& node : *nodes) {
                  auto node_text =
                      StringValue(AnyField(node, "orig_text"));
                  if (node_text.empty()) {
                    node_text = StringValue(AnyField(node, "text"));
                  }
                  text += node_text;
                }
                text = CleanText(text);
              }
            }
          }
          if (!text.empty()) return text;
        }
      }
    }
  }
  for (const char* key : {"orig", "orig_dyn", "origin", "forward"}) {
    const auto* nested = AnyField(item, key);
    if (nested != nullptr && nested->is_object()) {
      auto text = ExtractDynamicText(*nested, depth + 1);
      if (!text.empty()) return text;
    }
  }
  return {};
}

}  // namespace BilibiliDynamic
