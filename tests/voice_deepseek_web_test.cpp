#include "VoiceDeepSeekWeb.hpp"
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using nlohmann::json;
void Check(bool condition, const char* label) { if (!condition) throw std::runtime_error(label); }
void Error(const json& result, const char* label) {
  Check(result["ok"] == false && result["error"].is_string() && !result["error"].get<std::string>().empty(), label);
}
std::string Repeat(const std::string& value, size_t count) {
  std::string out;
  while (count--) out += value;
  return out;
}
json Entry(const std::string& url = "https://example.com/a?x=1+2&x=3#part") {
  return {{"title", "Title"}, {"url", url}, {"snippet", "Snippet"}, {"summary", "Summary"}};
}
json Search(const json& entries, size_t limit = 5) {
  return Voice::ParseDeepSeekSearch({{"results", entries}}, "query", limit);
}
template <typename F> void Invalid(F fn, const char* label) {
  try { fn(); } catch (const std::invalid_argument&) { return; }
  throw std::runtime_error(label);
}
}  // namespace

int main() {
  try {
    auto result = Search(json::array({Entry(), Entry()}));
    Check(result["ok"] == true && result["provider"] == "deepseek" && result["query"] == "query", "search envelope");
    Check(result["results"].size() == 1 && result["results"][0]["snippet"] == "Summary", "summary preference and deduplication");
    Check(result["results"][0]["url"] == Entry()["url"] && result["results"][0].size() == 3, "URL bytes and result fields");
    auto entry = Entry(); entry["summary"] = "";
    Check(Search(json::array({entry}))["results"][0]["snippet"] == "Snippet", "empty summary uses snippet");
    entry.erase("summary"); entry.erase("snippet");
    Check(Search(json::array({entry}))["results"][0]["snippet"] == "", "optional snippet");
    Check(Search(json::array())["ok"] == true, "explicit empty results succeed");
    for (const auto& body : {json{}, json::array(), json{{"error", "private remote error"}}, json{{"results", nullptr}},
                            json{{"results", json::array({42})}}})
      Error(Voice::ParseDeepSeekSearch(body, "query", 5), "malformed search body");
    for (const auto* field : {"title", "url", "summary", "snippet", "content", "site_name", "date"}) {
      entry = Entry(); entry[field] = 42;
      Error(Search(json::array({entry})), "malformed search fields cannot become empty success");
      Error(Search(json::array({Entry(), entry}), 1), "validate even entries past output limit");
    }
    entry = Entry(); entry["site_name"] = nullptr; entry["date"] = nullptr; entry["site_icon"] = nullptr;
    Check(Search(json::array({entry}))["ok"] == true, "documented nullable source metadata accepted");
    entry = Entry(); entry.erase("url");
    Error(Search(json::array({entry})), "missing required URL");
    entry = Entry(); entry["title"] = "  ";
    Error(Search(json::array({entry})), "empty title is malformed");
    for (const auto* url : {"javascript:alert(1)", "file:///tmp/test", "ftp://example.com", "/relative", "https://",
        "http:///example.com", "https://user:pass@example.com", "https://@example.com", "https://exa mple.com/",
        "https://example.com/\\path", "https://example.com/%zz", "https://example.com/%0a", "https://example.com/\n"}) {
      Check(Search(json::array({Entry(url)}))["results"].empty(), "invalid URLs skipped");
      Error(Voice::ParseDeepSeekFetch({{"title", "T"}, {"content", "body"}}, url), "invalid fetch URL rejected");
    }
    const auto chinese = Repeat("中", 128);
    const auto emoji = Repeat("😀", 128);
    Check(Voice::ParseDeepSeekSearch({{"results", json::array()}}, chinese, 5)["ok"] == true, "128 Chinese characters accepted");
    Check(Voice::ParseDeepSeekSearch({{"results", json::array()}}, emoji, 5)["ok"] == true, "128 four-byte characters accepted");
    for (const auto& query : {std::string{}, chinese + "中", emoji + "a", std::string("\xc0\xaf"), std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80")})
      Error(Voice::ParseDeepSeekSearch({{"results", json::array()}}, query, 5), "invalid UTF-8 or character count");
    json entries = json::array();
    for (int i = 0; i < 10; ++i) {
      entry = Entry("https://example.com/" + std::to_string(i));
      entry["title"] = Repeat("中", 100); entry["summary"] = Repeat("😀", 1000);
      entries.push_back(entry);
    }
    result = Search(entries, 99);
    Check(result["results"].size() == 5 && result.dump().size() < 12000, "search count and UTF-8 budget");
    Check(!json::parse(result.dump(), nullptr, false).is_discarded(), "search truncation valid UTF-8");
    Check(Search(entries, 1)["results"].size() == 1, "search requested limit");
    for (auto& item : entries) {
      item["url"] = item["url"].get<std::string>() + std::string(1800, 'a');
      item["summary"] = std::string(768, '\n');
    }
    Check(Search(entries).dump().size() < 12000, "search escaped JSON budget");

    const std::string url = "https://example.com/article";
    const std::string markdown = "# Title\n\n<https://example.com>\n```cpp\nif (a < b && b > c) {}\n```\n**中文**";
    result = Voice::ParseDeepSeekFetch({{"title", "Title"}, {"content", markdown}}, url);
    Check(result["ok"] == true && result["provider"] == "deepseek" && result["url"] == url &&
          result["format"] == "markdown" && result["content"] == markdown && result["truncated"] == false,
          "Markdown preserved without HTML stripping");
    for (const auto& content : {Repeat("中", 4000), Repeat("😀", 4000), "# T" + std::string(7997, '\n'), std::string(8000, '\x01')}) {
      result = Voice::ParseDeepSeekFetch({{"title", Repeat("中", 200)}, {"content", content}}, url);
      Check(result["ok"] == true && result["truncated"] == true && !result["content"].get<std::string>().empty() && result["content"].get<std::string>().size() <= 8000 &&
            result.dump().size() < 12000 && !json::parse(result.dump(), nullptr, false).is_discarded(), "fetch byte and escaped JSON budgets");
    }
    for (const auto& body : {json{}, json{{"content", "body"}}, json{{"title", 42}, {"content", "body"}},
         json{{"title", "T"}, {"content", nullptr}}, json{{"title", "T"}, {"content", ""}},
         json{{"title", "T"}, {"content", " \n\t"}}, json{{"title", "T"}, {"content", std::string("\xff")}},
         json{{"title", "T"}, {"content", "body"}, {"error", "secret"}}})
      Error(Voice::ParseDeepSeekFetch(body, url), "malformed or empty fetch body");
    Error(Voice::ParseDeepSeekFetch({{"title", "T"}, {"content", std::string(2 * 1024 * 1024, 'a')}}, url), "offline response cap");

    int calls = 0;
    const std::string key = "synthetic-test-key-not-a-real-credential";
    Voice::DeepSeekWebRequest outbound;
    std::string responseBody = json{{"results", json::array({Entry()})}}.dump();
    int status = 200;
    bool networkOk = true;
    const Voice::DeepSeekWebTransport transport = [&](const auto& request, const auto& write, const auto& progress) {
      ++calls; outbound = request;
      Check(progress(), "normal progress callback");
      write(responseBody);
      return Voice::DeepSeekWebReply{status, networkOk};
    };
    result = Voice::QueryDeepSeekSearch({{"query", chinese}}, key, {}, transport);
    Check(result["ok"] == true && calls == 1, "offline search adapter");
    Check(outbound.url == "https://api.deepseek.com/v1/tools/search" &&
          json::parse(outbound.body) == json{{"query", chinese}, {"count", 5}, {"need_content", false}}, "exact search endpoint and payload");
    Check(outbound.headers == std::map<std::string, std::string>{{"Authorization", "Bearer " + key}, {"Content-Type", "application/json"}}, "exact credential headers");
    Check(outbound.url.find(key) == std::string::npos && outbound.body.find(key) == std::string::npos, "secret confined to headers");
    Check(outbound.timeoutMs == 15000 && outbound.connectTimeoutMs == 3000 && !outbound.followRedirects, "timeouts and redirect policy");
    responseBody = json{{"title", "T"}, {"content", markdown}}.dump();
    result = Voice::QueryDeepSeekFetch({{"url", url}}, key, {}, transport);
    Check(result["ok"] == true && outbound.url == "https://api.deepseek.com/v1/tools/fetch" &&
          json::parse(outbound.body) == json{{"url", url}}, "fetch fixed endpoint, target only in body");
    const auto before = calls;
    Error(Voice::QueryDeepSeekFetch({{"url", url}}, "", {}, transport), "empty key readable error");
    for (const auto& invalid : {std::string("bad\r\nX-Secret: 1"), std::string("bad key"), std::string(513, 'a')})
      Error(Voice::QueryDeepSeekFetch({{"url", url}}, invalid, {}, transport), "invalid key rejected");
    Error(Voice::QueryDeepSeekSearch({{"query", "q"}}, key, [] { return true; }, transport), "pre-cancelled request");
    Check(calls == before, "invalid credentials and pre-cancellation do not call transport");
    for (const auto& request : {json{{"query", chinese + "中"}}, json{{"query", "q"}, {"limit", 0}},
        json{{"query", "q"}, {"limit", 6}}, json{{"query", "q"}, {"limit", 1.5}}, json{{"query", "q"}, {"key", "x"}}})
      Invalid([&] { Voice::QueryDeepSeekSearch(request, key, {}, transport); }, "invalid search arguments");
    Invalid([&] { Voice::QueryDeepSeekFetch({{"url", "https://user:pass@example.com"}}, key, {}, transport); }, "fetch credentials rejected");
    Invalid([&] { Voice::QueryDeepSeekFetch({{"url", "https://example.com/" + std::string(2048, 'a')}}, key, {}, transport); }, "URL length bounded");
    responseBody = "private remote body with " + key;
    std::set<std::string> errors;
    for (const int code : {401, 402, 403, 404, 429, 500, 503, 302}) {
      status = code;
      const auto previous = calls;
      result = Voice::QueryDeepSeekFetch({{"url", url}}, key, {}, transport);
      Error(result, "HTTP status error");
      Check(result.dump().find(key) == std::string::npos && result.dump().find("private remote") == std::string::npos, "no error body leak");
      Check(calls == previous + 1, "no retry or fallback");
      errors.insert(result["error"].get<std::string>());
    }
    Check(errors.size() == 7, "distinct actionable HTTP errors; 5xx share service error");
    status = 200;
    for (const auto& body : {std::string("not JSON"), std::string("[]"), std::string("{\"error\":\"private remote message\"}")}) {
      responseBody = body;
      Error(Voice::QueryDeepSeekSearch({{"query", "q"}}, key, {}, transport), "malformed and error bodies fail");
    }
    networkOk = false;
    Error(Voice::QueryDeepSeekSearch({{"query", "q"}}, key, {}, transport), "network failure");
    const Voice::DeepSeekWebTransport oversize = [](const auto&, const auto& write, const auto&) {
      Check(write(std::string(2 * 1024 * 1024, 'a')), "exact response byte cap accepted");
      Check(!write("x") && !write(""), "oversize response aborts and remains stopped");
      return Voice::DeepSeekWebReply{200, false};
    };
    Error(Voice::QueryDeepSeekFetch({{"url", url}}, key, {}, oversize), "streamed response cap");
    for (bool viaProgress : {false, true}) {
      bool cancelled = false;
      const Voice::DeepSeekWebTransport cancel = [&](const auto&, const auto& write, const auto& progress) {
        Check(write("{"), "first chunk before cancellation");
        cancelled = true;
        Check(!(viaProgress ? progress() : write("}")), "cancellation aborts progress and write");
        cancelled = false;
        Check(!progress() && !write("}"), "cancellation latched");
        return Voice::DeepSeekWebReply{200, false};
      };
      result = Voice::QueryDeepSeekFetch({{"url", url}}, key, [&] { return cancelled; }, cancel);
      Error(result, "in-flight cancellation");
      Check(result["error"].get<std::string>().find("取消") != std::string::npos, "cancellation reported rather than network error");
    }
    const Voice::DeepSeekWebTransport throwing = [&](const auto&, const auto&, const auto&) -> Voice::DeepSeekWebReply {
      throw std::runtime_error(key);
    };
    result = Voice::QueryDeepSeekFetch({{"url", url}}, key, {}, throwing);
    Error(result, "transport exceptions sanitized");
    Check(result.dump().find(key) == std::string::npos, "exception cannot leak key");
    std::cout << "DeepSeek web tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
