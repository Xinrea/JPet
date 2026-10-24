#include "VoiceWebFetch.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using nlohmann::json;
constexpr const char* Url = "https://example.com/article";
void Check(bool condition, const char* label) { if (!condition) throw std::runtime_error(label); }
void Error(const json& result, const char* label) {
  Check(result.at("ok") == false && result.at("provider") == "local" &&
        result.at("error").is_string() && !result.at("error").get<std::string>().empty(), label);
}
json Html(const std::string& body) { return Voice::ParseWebPage(body, "text/html; charset=utf-8", Url); }
void Success(const json& result) {
  Check(result.at("ok") == true && result.at("provider") == "local" && result.at("url") == Url &&
        result.at("format") == "text" && result.at("title").is_string() && result.at("truncated").is_boolean(), "success envelope");
  Check(result.at("notice").get<std::string>().find("外部网页数据") != std::string::npos, "untrusted notice");
  Check(result.at("content").get<std::string>().size() <= 8000 && result.dump().size() < 12000, "output budgets");
  Check(json::parse(result.dump()) == result, "valid UTF-8 JSON round trip");
}
}

int main() {
  try {
    auto result = Html("<!doctype html><HTML><HEAD><title>Example &amp; &#20013;&#x6587;</title>"
      "<meta name='description' content='hidden'><style>body { content: '<x>'; }</style>"
      "<script>if (1 < 2) { const markup = '<p>script</p>'; }</script>head secret</HEAD>"
      "<body><!-- comment secret --><form><div>form secret</div><input value='secret'><form>nested secret</form></form>"
      "<noscript>noscript secret</noscript><template>template secret</template><svg><text>svg secret</text></svg>"
      "<iframe>frame secret</iframe><object>object secret</object><canvas>canvas secret</canvas>"
      "<select><option>select secret</option></select><textarea>textarea secret <b>raw</b></textarea>"
      "<button>button secret</button><h1 data-value='a > b'>Heading</h1><p>Hello <b>read</b>er.</p>"
      "<p>Next<br>line</p><script>script secret</script><style>style secret</style></body></HTML>");
    Success(result);
    Check(result["title"] == "Example & 中文", "title extraction and entities");
    Check(result["content"] == "Heading Hello reader. Next line" && result["truncated"] == false, "HTML cleanup and inline boundaries");

    result = Html("<p>&quot;quoted&quot; &apos;single&apos; &amp; &lt;literal&gt; &amp;lt; &#x1F600; "
                  "one&nbsp;  two &mdash; &ndash; &hellip; &copy; &unknown;</p>");
    Success(result);
    Check(result["content"] == "\"quoted\" 'single' & <literal> &lt; 😀 one two — – … © &unknown;", "single-pass entity decoding");
    result = Html("<p>&#0; &#xD800; &#x110000; &#999999999999999999999; &#xzz;</p>");
    Success(result);
    Check(result["content"].get<std::string>().find("�") != std::string::npos, "invalid numeric codepoints replaced");
    result = Html("<head><title>Implicit head end</title><body><p>Visible paragraph");
    Success(result);
    Check(result["content"] == "Visible paragraph", "optional HTML end tags tolerated");
    result = Html("<script>hidden </scripture> still hidden</script><p>visible</p>");
    Success(result);
    Check(result["content"] == "visible", "raw-text closing name boundary");

    result = Voice::ParseWebPage("  A plain line\n第二行 &amp; <tag>\tend  ", " TEXT/PLAIN ; charset=UTF-8", Url);
    Success(result);
    Check(result["title"] == "" && result["content"] == "A plain line\n第二行 &amp; <tag>\tend", "plain text preserves line breaks and literal markup");

    std::string multilingual;
    for (int i = 0; i < 2000; ++i) multilingual += "中😀";
    for (const auto* mime : {"text/plain", "text/html"}) {
      result = Voice::ParseWebPage(multilingual, mime, Url);
      Success(result);
      Check(result["truncated"] == true && result["content"].get<std::string>().size() >= 7996, "UTF-8 safe 8000-byte truncation");
    }
    result = Voice::ParseWebPage(std::string(8000, '"'), "text/plain", Url);
    Success(result);
    Check(result["truncated"] == true, "JSON escaping counts toward serialized cap");
    result = Html("<title>" + multilingual + "</title><p>body</p>");
    Success(result);
    Check(result["title"].get<std::string>().size() <= 256 && result["truncated"] == true, "UTF-8 title cap");
    result = Voice::ParseWebPage(std::string(8000, '\\'), "text/plain", std::string("https://example.com/") + std::string(2000, 'x'));
    Check(result["ok"] == true && result.dump().size() < 12000 && result["truncated"] == true, "long URL and escaping share JSON budget");
    result = Voice::ParseWebPage(std::string(8000, '"'), "text/plain", std::string("https://example.com/") + std::string(2000, '"'));
    Check(result["ok"] == true && result.dump().size() < 12000 && !result["content"].get<std::string>().empty(), "escaped metadata cannot discard all content");
    result = Voice::ParseWebPage(std::string(2 * 1024 * 1024, 'x'), "text/plain", Url);
    Success(result);
    Error(Voice::ParseWebPage(std::string(2 * 1024 * 1024 + 1, 'x'), "text/plain", Url), "2 MiB input limit");
    for (const auto* mime : {"", "application/json", "application/pdf", "image/png", "application/octet-stream", "text/xml", "text/htmlish"})
      Error(Voice::ParseWebPage("body", mime, Url), "unsupported MIME rejected");
    for (const auto* text : {"", " \n\t ", "<html><head><title>Only a title</title></head><body></body></html>",
         "<script>document.write('hello')</script>", "<form>hidden</form>", "<!-- incomplete", "<script>unclosed",
         "<p>visible</p><form>unclosed hidden", "<p title='unfinished>text"}) Error(Html(text), "empty/unextractable HTML rejected");
    Error(Voice::ParseWebPage(std::string("plain\0binary", 12), "text/plain", Url), "NUL binary rejected");
    Error(Voice::ParseWebPage("bad\xff", "text/plain", Url), "invalid UTF-8 rejected");
    Error(Html("<p>bad\xed\xa0\x80</p>"), "UTF-8 surrogate rejected");
    for (const auto* text : {"<title>Just a moment...</title><p>Wait</p>", "<div class='g-recaptcha'>Check</div>",
         "<script src='/cdn-cgi/challenge-platform/a'></script><p>Wait</p>", "<p>Verify you are human</p>",
         "<title>Access denied</title><p>No access</p>"}) Error(Html(text), "challenge rejected");
    Error(Voice::ParseWebPage("Checking your browser before continuing", "text/plain", Url), "plain text challenge rejected");
    Success(Html("<h1>Writing a CAPTCHA tutorial</h1><p>Ordinary article content.</p>"));
    const std::string guide = "reCAPTCHA integration guide: add class=\"g-recaptcha\" or class=\"h-captcha\". Example id=\"challenge-form\".";
    result = Voice::ParseWebPage(guide, "text/plain", Url);
    Success(result);
    Check(result["content"] == guide, "plain text CAPTCHA documentation is not an HTML challenge");
    result = Html("<article><h1>Readable article</h1><p>Useful content.</p></article>"
      "<form><div class='g-recaptcha'>Contact verification</div></form>");
    Success(result);
    Check(result["content"] == "Readable article Useful content.", "contact form CAPTCHA does not discard article");
    result = Html("<p>Article before</p><div data-sitekey='test' class='widget g-recaptcha'>"
      "Check<div><span>nested widget</span></div><script>const x = '</div>';</script></div><p>Article after</p>");
    Success(result);
    Check(result["content"] == "Article before Article after", "standalone CAPTCHA widget is excluded without losing surrounding text");
    result = Html("<p>Documentation: &lt;div class=&quot;g-recaptcha&quot;&gt;widget&lt;/div&gt;</p>");
    Success(result);
    Check(result["content"].get<std::string>().find("g-recaptcha") != std::string::npos, "quoted CAPTCHA markup is preserved");
    Error(Html("<div class='h-captcha'><div>Check</div></div>"), "widget-only challenge remains unreadable");

    using Voice::WebFetchDetail::IsPublicAddress;
    for (const auto* ip : {"8.8.8.8", "1.1.1.1", "93.184.216.34", "100.63.255.255", "100.128.0.1", "172.15.0.1", "172.32.0.1",
         "2001:4860:4860::8888", "2606:4700:4700::1111", "::ffff:8.8.8.8"}) Check(IsPublicAddress(ip), "public IP allowed");
    for (const auto* ip : {"", "example.com", "127.1", "0.0.0.0", "0.1.2.3", "10.0.0.1", "100.64.0.1", "100.127.255.255",
         "127.0.0.1", "169.254.169.254", "172.16.0.1", "172.31.255.255", "192.0.0.1", "192.0.2.1", "192.88.99.1", "192.168.1.1",
         "198.18.0.1", "198.19.255.255", "198.51.100.1", "203.0.113.1", "224.0.0.1", "240.0.0.1", "255.255.255.255",
         "::", "::1", "::127.0.0.1", "::ffff:127.0.0.1", "::ffff:a00:1", "::ffff:169.254.169.254", "::ffff:192.168.0.1",
         "fc00::1", "fd00::1", "fe80::1", "fec0::1", "ff02::1", "64:ff9b::a00:1", "100::1", "2001::1", "2001:20::1",
         "2001:db8::1", "2002:7f00:1::", "3fff::1", "3fff:fff::1", "5f00::1", "fe80::1%en0"}) Check(!IsPublicAddress(ip), "non-public IP rejected");

    using Voice::WebFetchDetail::IsAllowedUrl;
    for (const auto* url : {"https://example.com", "http://example.com:8080/a", "https://8.8.8.8/", "https://[2606:4700:4700::1111]/",
         "https://example.com/a%20b?q=one%2Ftwo&x=1#part"}) Check(IsAllowedUrl(url), "public URL syntax allowed without resolving DNS");
    for (const auto* url : {"", "not a url", "//example.com", "file:///etc/passwd", "ftp://example.com/", "https://", "http:///example.com",
         "https://user:password@example.com/", "https://@example.com/", "https://user@example.com/", "http://localhost/", "http://localhost./",
         "http://sub.localhost/", "http://127.0.0.1/", "http://127.1/", "http://2130706433/", "http://0177.0.0.1/", "http://0x7f000001/",
         "http://10.0.0.1/", "http://169.254.169.254/", "http://[::1]/", "http://[::ffff:127.0.0.1]/", "http://[fe80::1%25en0]/",
         "https://example.com/\\path", "https://example.com/\npath", "https://exa mple.com/", "https://example.com/%0a", "https://example.com/%00",
         "https://example.com/%5c", "https://example.com/%7f", "https://example.com/%zz", "https://example.com/%", "https://example.com:99999/"}) {
      Check(!IsAllowedUrl(url), "invalid/non-public URL rejected");
      // These must all reject before attempting any DNS or socket operation.
      Error(Voice::FetchWebPage(url), "offline rejected fetch");
    }
    Error(Voice::FetchWebPage(std::string("https://example.com/\0suffix", 27)), "embedded NUL URL rejected");
    Error(Voice::FetchWebPage(std::string("https://example.com/") + std::string(2048, 'x')), "oversize URL rejected");
    auto cancelled = Voice::FetchWebPage(Url, [] { return true; });
    Error(cancelled, "pre-cancelled fetch");
    Check(cancelled["error"] == "网页读取已取消", "specific cancellation error");
    Error(Voice::FetchWebPage(Url, []() -> bool { throw std::runtime_error("private detail"); }), "throwing cancellation fails closed");
    Check(Voice::FetchWebPage("https://user:password@example.com/").dump().find("password") == std::string::npos, "credentials never echoed in errors");
    std::cout << "voice_web_fetch_test passed (offline)\n";
  } catch (const std::exception& e) {
    std::cerr << "voice_web_fetch_test failed: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
