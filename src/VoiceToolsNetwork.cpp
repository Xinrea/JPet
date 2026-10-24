#include "VoiceToolsNetwork.hpp"
#include "VoiceTools.hpp"
#include "VoiceBilibili.hpp"
#include "VoiceWebSearch.hpp"
#include "VoiceToolArgs.hpp"
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
json QueryWebSearch(const json& request, const std::function<bool()>& cancelled) {
  // Fixed HTTPS endpoint only; never send JPet cookies, model keys, or follow result links.
  ToolArgs::Fields(request, {"query", "limit"});
  const auto query = ToolArgs::Text(request, "query", 512);
  if (query.empty()) return Fail("请提供搜索关键词query");
  const auto limit = ToolArgs::Number(request, "limit", 1, 5, 5);
  const auto live = [&] { return !cancelled || !cancelled(); };
  if (!live()) return Fail("网页搜索已取消");
  std::string received;
  bool tooLarge = false;
  const cpr::WriteCallback write{[&](std::string data, intptr_t) {
    if (!live()) return false;
    if (received.size() + data.size() > 2 * 1024 * 1024) { tooLarge = true; return false; }
    received += data;
    return true;
  }};
  const cpr::ProgressCallback progress{[&](cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, intptr_t) {
    return live();
  }};
  const auto response = cpr::Get(cpr::Url{"https://html.duckduckgo.com/html/"},
    cpr::Parameters{{"q", query}}, cpr::Header{{"User-Agent", "JPet/3.0 (web search)"}, {"Accept", "text/html"}},
    cpr::Timeout{15000}, cpr::ConnectTimeout{3000}, cpr::Redirect{false}, write, progress);
  if (!live()) return Fail("网页搜索已取消");
  if (tooLarge) return Fail("DuckDuckGo搜索响应过大，请缩小搜索范围");
  if (response.status_code == 202 || response.status_code == 403 || response.status_code == 429)
    return Fail("DuckDuckGo要求验证或暂时限流，请稍后重试");
  if (response.error.code != cpr::ErrorCode::OK) return Fail("网页搜索网络请求失败或超时，请稍后重试");
  if (response.status_code != 200) return Fail("DuckDuckGo搜索服务暂时不可用，请稍后重试");
  return ParseDuckDuckGoSearch(received, query, static_cast<size_t>(limit));
}

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
