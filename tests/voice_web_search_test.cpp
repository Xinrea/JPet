#include "VoiceWebSearch.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using nlohmann::json;
void Check(bool condition, const char* label) { if (!condition) throw std::runtime_error(label); }
std::string Entry(const std::string& href, const std::string& title = "Example", const std::string& snippet = "A snippet",
                  const std::string& extraClass = "") {
  return "<div class='result results_links results_links_deep web-result " + extraClass + "'>"
    "<div class='links_main links_deep result__body'><h2 class='result__title'>"
    "<a rel='nofollow' href='" + href + "' class='result__a'>" + title + "</a></h2>"
    "<a class='result__snippet' href='" + href + "'>" + snippet + "</a></div></div>";
}
std::string Page(const std::string& content) {
  return "<!DOCTYPE html PUBLIC '-//W3C//DTD XHTML 1.0 Transitional//EN' 'http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd'>"
    "<html><head><meta charset='utf-8'><title>Search at DuckDuckGo</title>"
    "<script>if (1 < 2) { var ignored = '<a class=\"result__a\">'; }</script>"
    "</head><body><form action='/html/'><input name='q' value='example'></form>"
    "<!-- <a class='result__a' href='https://ignored.example'>comment</a> -->"
    "<div id='links' class='results'>" + content + "</div></body></html>";
}
json Parse(const std::string& content, size_t limit = 5) {
  return Voice::ParseDuckDuckGoSearch(Page(content), "test query", limit);
}
void Error(const std::string& html, const char* label) {
  auto result = Voice::ParseDuckDuckGoSearch(html, "test", 5);
  Check(result["ok"] == false && result["error"].is_string() && !result["error"].get<std::string>().empty(), label);
}
std::string Percent(const std::string& value) {
  static constexpr char hex[] = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : value) { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
  return out;
}
}

int main() {
  try {
    auto result = Parse(Entry("//duckduckgo.com/l/?uddg=https%3A%2F%2Fen.cppreference.com%2Fcpp&amp;rut=abc",
                              "C++ <b>reference</b> - cppreference.com", "Search <b>C++</b> documentation."));
    Check(result["ok"] == true && result["provider"] == "duckduckgo" && result["query"] == "test query", "result envelope");
    Check(result["notice"].is_string() && !result["notice"].get<std::string>().empty(), "external snippet notice");
    Check(result["results"].size() == 1 && result["results"][0] == json({
      {"title", "C++ reference - cppreference.com"}, {"url", "https://en.cppreference.com/cpp"},
      {"snippet", "Search C++ documentation."}}), "real DDG shape, nested markup and protocol-relative redirect");

    result = Parse("<DIV class=\"result\"><H2><A data-label=\"x > y\" CLASS=\"other result__a\" HREF=\"https://example.com/?a=1&amp;b=2\">"
      "&#20013;&#x6587; &quot;quote&quot; &apos;single&apos; &amp; &lt;literal&gt; &amp;lt; &#x1F600;</A></H2>"
      "<div class='result__snippet'>one&nbsp; two\nthree <span>four<b>five</b></span><br>six &mdash; seven</div></DIV>");
    Check(result["results"][0]["title"] == "中文 \"quote\" 'single' & <literal> &lt; 😀", "numeric, named and single-pass text entity decoding");
    Check(result["results"][0]["snippet"] == "one two three fourfive six — seven", "whitespace and nested inline text");
    Check(result["results"][0]["url"] == "https://example.com/?a=1&b=2", "href entity decoding");

    result = Parse("<h2><a class='result__a' href='https://one.example'>One</a></h2>"
      "<div class='result__snippet'>First</div><h2><a class='result__a' href='https://two.example'>Two</a></h2>"
      "<div class='result__snippet'>Second</div>");
    Check(result["results"].size() == 2 && result["results"][0]["snippet"] == "First" &&
          result["results"][1]["snippet"] == "Second", "snippet association without optional result wrappers");

    const std::string signedUrl = "https://example.com/a%2Fb?X-Amz-Signature=A%2Bb%2Fz%3D&x=1+2&x=3#part";
    result = Parse(Entry("/l/?uddg=" + Percent(signedUrl) + "&amp;rut=ignored"));
    Check(result["results"][0]["url"] == signedUrl, "signed query bytes and duplicate keys preserved");
    result = Parse(Entry("https://example.com/a&#x3F;x&#61;1&#38;y=2"));
    Check(result["results"][0]["url"] == "https://example.com/a?x=1&y=2", "numeric entities in href");
    const std::string externalRedirect = "https://other.example/l/?uddg=https%3A%2F%2Fdestination.example";
    result = Parse(Entry(externalRedirect));
    Check(result["results"][0]["url"] == externalRedirect, "non-DDG redirect is never unwrapped");
    result = Parse(Entry("//duckduckgo.com.evil.example/l/?uddg=https%3A%2F%2Fdestination.example"));
    Check(result["results"][0]["url"] == "https://duckduckgo.com.evil.example/l/?uddg=https%3A%2F%2Fdestination.example", "redirect host uses exact match");
    result = Parse(Entry("//example.com/path"));
    Check(result["results"][0]["url"] == "https://example.com/path", "protocol-relative HTTP result");

    for (const auto& url : {"javascript:alert(1)", "data:text/html,unsafe", "file:///tmp/test", "ftp://example.com", "/relative",
         "https://", "http:///example.com", "https://user:pass@example.com", "https://@example.com", "https://exa mple.com/",
         "https://example.com/\\path", "https://example.com/%zz", "https://example.com/%0a", "https://example.com/&#10;",
         "/l/?uddg=javascript%3Aalert(1)", "/l/?uddg=https%3A%2F%2Fexample.com%2F%250A", "/l/?rut=missing",
         "/l/?uddg=https%3A%2F%2Fone.example&amp;uddg=https%3A%2F%2Ftwo.example",
         "/l/?uddg=https%3A%2F%2Fduckduckgo.com%2Fl%2F%3Fuddg%3Dx", "https://duckduckgo.com/y.js?ad=1"}) {
      result = Parse(Entry(url));
      Check(result["ok"] == true && result["results"].empty(), "unsafe or malformed URL is skipped");
    }
    result = Parse(Entry("https://ad.example", "Ad", "Paid", "result--ad") +
                   Entry("https://badge-ad.example", "Ad", "<span class='result__badge--ad'>Ad</span>") +
                   Entry("https://example.com", "First") + Entry("https://example.com", "Duplicate") +
                   Entry("/l/?uddg=https%3A%2F%2Fexample.com", "Wrapped duplicate") + Entry("https://empty.example", " "));
    Check(result["results"].size() == 1 && result["results"][0]["title"] == "First", "ads, empty titles and URL duplicates skipped");

    result = Parse("<div class='result result--no-result'><div class='no-results'><div class='no-results__message'>"
                   "<h1>No results found for <strong>nonsense</strong></h1></div></div></div>");
    Check(result["ok"] == true && result["results"].empty(), "explicit no-results succeeds");
    Error(Page("<div class='no-results'></div>"), "empty marker alone is not an explicit no-results response");
    Error(Page("<form id='challenge-form' action='//duckduckgo.com/anomaly.js'><div class='anomaly-modal__title'>"
               "Unfortunately, bots use DuckDuckGo too.</div></form>"), "DDG challenge is not empty success");
    Error(Page("<div id='captcha'>Verify you are human</div>" + Entry("https://example.com")), "challenge takes priority over results");
    Error(Page("<div>Service unavailable</div>"), "unrecognized service response");
    Error("", "empty response");
    Error(Page("<div class='result'><a class='result__a' href='https://example.com'>truncated"), "unclosed result markup");
    Error("<div class='result'><a class='result__a' href='https://example.com>broken</a></div>", "unclosed attribute quote");
    Error(Page(Entry("https://example.com")) + "<!-- truncated", "truncation after results is still an error");
    Error(Page(Entry("https://example.com")) + "<div class='no-results'>No results found</div>", "contradictory page fails closed");
    Error(std::string(2 * 1024 * 1024 + 1, 'x'), "HTML input bound");
    Error(Page(Entry("https://example.com", std::string("bad\xff", 4))), "invalid UTF-8 HTML");
    Check(Voice::ParseDuckDuckGoSearch(Page(Entry("https://example.com")), std::string(1025, 'q'), 5)["ok"] == false, "query bound");

    std::string entries;
    for (int i = 0; i < 8; ++i) entries += Entry("https://example.com/" + std::to_string(i));
    Check(Parse(entries, 100)["results"].size() == 5, "hard five-result cap");
    Check(Parse(entries, 2)["results"].size() == 2, "requested result limit");
    Check(Parse(entries, 0)["results"].empty(), "zero limit");
    Error("<div id='captcha'>Verification</div>", "challenge checked even without valid results");
    Check(Voice::ParseDuckDuckGoSearch("<div id='captcha'>Verification</div>", "q", 0)["ok"] == false, "zero limit does not bypass classification");

    const std::string longUrl = "https://example.com/" + std::string(2048, 'x');
    Check(Parse(Entry(longUrl))["results"].empty(), "oversized URLs are skipped, not truncated");
    std::string multilingual;
    for (int i = 0; i < 1000; ++i) multilingual += "中文😀&quot;";
    entries.clear();
    for (int i = 0; i < 8; ++i) entries += Entry("https://example.com/" + std::to_string(i) + "?signed=" + std::string(1950, 'x'), multilingual, multilingual);
    result = Parse(entries);
    Check(result["ok"] == true && !result["results"].empty() && result["results"].size() < 5 && result.dump().size() < 12000, "serialized response budget");
    for (const auto& item : result["results"]) {
      Check(item["title"].get<std::string>().size() <= 256 && item["snippet"].get<std::string>().size() <= 768, "text byte caps");
      Check(json::parse(item.dump()) == item, "truncated text remains valid UTF-8");
      Check(item["url"].get<std::string>().size() > 1950, "budget never truncates URL signatures");
    }
    // Escaping can expand serialized bytes even when every field is byte-bounded.
    const auto quotedQuery = std::string(1024, '"');
    result = Voice::ParseDuckDuckGoSearch(Page(entries), quotedQuery, 5);
    Check(result["query"] == quotedQuery && result.dump().size() < 12000, "query escaping participates in JSON budget");
    std::string deep;
    for (int i = 0; i < 130; ++i) deep += "<div>";
    deep += Entry("https://example.com");
    for (int i = 0; i < 130; ++i) deep += "</div>";
    Error(deep, "bounded nesting depth");
    std::cout << "Voice offline web search parser tests passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
