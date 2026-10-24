#include "VoiceToolsNetwork.hpp"
#include "VoiceTools.hpp"
#include "VoiceBilibili.hpp"
#include "BilibiliDynamic.hpp"
#include <cpr/cpr.h>
#include <algorithm>
#include <ctime>
#include <map>

namespace Voice {
namespace {
using nlohmann::json;
json Fail(const std::string& error) { return {{"ok", false}, {"error", error}}; }
const char* const kBilibiliApiHosts[] = {"https://api.bilibili.com/", "https://api.live.bilibili.com/", "https://api.vc.bilibili.com/"};
struct HttpResult { json body; int status = 0; std::string error; };

HttpResult Request(const std::string& url, const cpr::Header& headers) {
  std::string received;
  const cpr::WriteCallback write{[&](std::string data, intptr_t) {
    if (received.size() + data.size() > 2 * 1024 * 1024) return false;
    received += data;
    return true;
  }};
  // Fixed HTTPS hosts only, TLS validation on, credentials never follow redirects.
  const auto response = cpr::Get(cpr::Url{url}, headers, cpr::Timeout{15000}, cpr::ConnectTimeout{3000}, cpr::Redirect{false}, write);
  HttpResult result;
  result.status = static_cast<int>(response.status_code);
  if (response.error.code != cpr::ErrorCode::OK) result.error = "工具网络请求失败或超时，请稍后重试";
  else {
    result.body = json::parse(received, nullptr, false);
    if (!result.body.is_object()) result.error = "工具服务未返回有效数据，请稍后重试";
  }
  return result;
}

} // namespace
json QueryBilibili(const json& request, const std::string& cookies, const std::string& uid) {
  if (cookies.size() > 65536 || cookies.find_first_of("\r\n") != std::string::npos) return Fail("B站登录信息格式异常，请重新登录");
  const auto built = BilibiliDynamic::BuildHeaders(cookies, BilibiliDynamic::kDefaultUserAgent, {},
    BilibiliDynamic::FetchBuvid3(BilibiliDynamic::kDefaultUserAgent));
  cpr::Header credentialed;
  for (const auto& [key, value] : built) credentialed[key] = value;
  const BilibiliGet get = [&](const std::string& url, const std::string& referer) -> BilibiliReply {
    if (url.compare(0, 8, "https://") != 0) return {{}, 0, "B站工具只访问 HTTPS 接口"};
    // Cookies go only to Bilibili's API hosts, never to CDN or other hosts.
    const bool api = std::any_of(std::begin(kBilibiliApiHosts), std::end(kBilibiliApiHosts),
      [&](const char* host) { return url.compare(0, std::char_traits<char>::length(host), host) == 0; });
    cpr::Header headers = api ? credentialed : cpr::Header{{"User-Agent", BilibiliDynamic::kDefaultUserAgent}};
    headers["Referer"] = referer;
    auto response = Request(url, headers);
    return {std::move(response.body), response.status, std::move(response.error)};
  };
  return RunBilibili(request, uid, get, static_cast<int64_t>(std::time(nullptr)));
}
} // namespace Voice
