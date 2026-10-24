#pragma once

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace BilibiliLogin {

inline constexpr const char* UserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36";
inline constexpr int MaxRedirects = 5;
using Cookies = std::map<std::string, std::string>;

inline std::string_view Trim(std::string_view value) {
  const auto start = value.find_first_not_of(" \t");
  if (start == std::string_view::npos) return {};
  return value.substr(start, value.find_last_not_of(" \t") - start + 1);
}

inline void AddCookie(Cookies& cookies, std::string_view header) {
  const auto pair = header.substr(0, header.find(';'));
  const auto equals = pair.find('=');
  if (equals == std::string_view::npos) return;
  const auto name = Trim(pair.substr(0, equals));
  if (name == "DedeUserID" || name == "DedeUserID__ckMd5" ||
      name == "SESSDATA" || name == "bili_jct" || name == "sid") {
    // Cookie values are opaque: do not URL-decode SESSDATA or split on '='.
    cookies[std::string(name)] = std::string(Trim(pair.substr(equals + 1)));
  }
}

inline bool Complete(const Cookies& cookies) {
  for (const char* name : {"DedeUserID", "SESSDATA", "bili_jct"}) {
    const auto it = cookies.find(name);
    if (it == cookies.end() || it->second.empty()) return false;
  }
  const auto& uid = cookies.at("DedeUserID");
  return uid.find_first_not_of("0123456789") == std::string::npos &&
         uid.find_first_not_of('0') != std::string::npos;
}

inline std::string CookieHeader(const Cookies& cookies) {
  std::string result;
  for (const auto& [name, value] : cookies) {
    if (!result.empty()) result += "; ";
    result += name + "=" + value;
  }
  return result;
}

inline bool ParseHttpsUrl(const std::string& url, std::string& host,
                          std::string& path) {
  constexpr std::string_view prefix = "https://";
  if (url.compare(0, prefix.size(), prefix) != 0 ||
      url.find_first_of("\r\n\t \\") != std::string::npos) return false;
  const auto authorityEnd = url.find_first_of("/?#", prefix.size());
  host = url.substr(prefix.size(), authorityEnd == std::string::npos
                                       ? std::string::npos
                                       : authorityEnd - prefix.size());
  if (host.empty() || host.find_first_not_of(
          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") !=
          std::string::npos) return false;
  std::transform(host.begin(), host.end(), host.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (authorityEnd == std::string::npos || url[authorityEnd] == '#') {
    path = "/";
  } else {
    path = (url[authorityEnd] == '?' ? "/" : "") + url.substr(authorityEnd);
    path = path.substr(0, path.find('#'));
  }
  return true;
}

inline bool AllowedHost(const std::string& host) {
  // This ticket exchanger is not a browser cookie jar. Only forward collected
  // login credentials to the explicitly trusted authentication destinations.
  return host == "passport.bilibili.com" ||
         host == "passport-api.bilibili.com" ||
         host == "account.bilibili.com" || host == "www.bilibili.com" ||
         host == "bilibili.com";
}

inline std::string RedirectUrl(const std::string& host, const std::string& path,
                               const std::string& location) {
  if (location.empty()) return {};
  if (location.compare(0, 2, "//") == 0) return "https:" + location;
  // Return absolute URLs unchanged so ParseHttpsUrl rejects HTTP and other schemes.
  const auto colon = location.find(':');
  if (colon != std::string::npos && colon < location.find_first_of("/?#"))
    return location;
  const auto origin = "https://" + host;
  const auto basePath = path.substr(0, path.find('?'));
  if (location.front() == '/') return origin + location;
  if (location.front() == '?') return origin + basePath + location;
  if (location.front() == '#') return origin + path + location;

  // Resolve relative paths, including ../, as a browser would.
  const auto target = basePath.substr(0, basePath.rfind('/') + 1) + location;
  const auto end = target.find_first_of("?#");
  const auto targetPath = target.substr(0, end);
  std::vector<std::string> segments;
  size_t start = 1;
  while (start <= targetPath.size()) {
    const auto slash = targetPath.find('/', start);
    const auto segment = targetPath.substr(start, slash - start);
    if (segment == "..") {
      if (!segments.empty()) segments.pop_back();
    } else if (segment != ".") {
      segments.push_back(segment);
    }
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  std::string resolved = origin + "/";
  for (size_t i = 0; i < segments.size(); ++i) {
    if (i) resolved += '/';
    resolved += segments[i];
  }
  if ((targetPath.size() >= 2 && targetPath.compare(targetPath.size() - 2, 2, "/.") == 0) ||
      (targetPath.size() >= 3 && targetPath.compare(targetPath.size() - 3, 3, "/..") == 0)) {
    if (resolved.back() != '/') resolved += '/';
  }
  if (end != std::string::npos) resolved += target.substr(end);
  return resolved;
}

struct Response {
  int status = 0;
  std::vector<std::string> setCookies;
  std::string location;
};

struct Result {
  Cookies cookies;
  // Diagnostic contains no ticket URLs or credential values.
  std::string error;
};

// Called only after the poll's HTTP status and JSON login code indicate success.
// Get(host, path, cookieHeader) returns optional<Response>. Keeping transport
// separate lets tests replay the same poll/redirect workflow without real login.
template <typename Get>
Result Resolve(const Response& poll, const std::string& loginUrl, Get get) {
  Cookies cookies;
  for (const auto& value : poll.setCookies) AddCookie(cookies, value);
  if (Complete(cookies)) return {std::move(cookies), {}};

  std::string currentUrl = loginUrl;
  for (int redirects = 0; redirects <= MaxRedirects; ++redirects) {
    std::string host, path;
    if (!ParseHttpsUrl(currentUrl, host, path))
      return {{}, "invalid HTTPS login URL"};
    if (!AllowedHost(host)) return {{}, "untrusted login host: " + host};
    const auto response = get(host, path, CookieHeader(cookies));
    if (!response) return {{}, "cookie exchange request failed at " + host};
    if (response->status < 200 || response->status >= 400)
      return {{}, "cookie exchange HTTP " + std::to_string(response->status) + " at " + host};
    for (const auto& value : response->setCookies) AddCookie(cookies, value);
    if (Complete(cookies)) return {std::move(cookies), {}};
    if (response->status < 300 || response->location.empty()) {
      std::string missing;
      for (const char* name : {"DedeUserID", "SESSDATA", "bili_jct"}) {
        const auto it = cookies.find(name);
        if (it == cookies.end() || it->second.empty()) {
          if (!missing.empty()) missing += ", ";
          missing += name;
        }
      }
      return {{}, missing.empty() ? "invalid account UID" : "missing cookies: " + missing};
    }
    if (redirects == MaxRedirects) return {{}, "too many login redirects"};
    currentUrl = RedirectUrl(host, path, response->location);
  }
  return {{}, "too many login redirects"};
}

}  // namespace BilibiliLogin
