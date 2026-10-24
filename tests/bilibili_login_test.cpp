#include "BilibiliLogin.hpp"

#include <cassert>
#include <iostream>

using namespace BilibiliLogin;

int main() {
  const Response complete{200, {"DedeUserID=123; Path=/", "SESSDATA=value%2Cwith==; HttpOnly",
                                "bili_jct=csrf; Secure"}, {}};
  int requests = 0;
  const auto noRequest = [&](const auto&, const auto&, const auto&)
      -> std::optional<Response> {
    ++requests;
    return std::nullopt;
  };

  // Regression: the poll itself can carry all cookies, even without a ticket URL.
  const auto direct = Resolve(complete, "", noRequest);
  assert(direct.error.empty());
  assert(direct.cookies.at("SESSDATA") == "value%2Cwith==");
  assert(requests == 0);

  // JLiverTool's ticket-only flow still works when the poll has no cookies.
  const auto ticketOnly = Resolve({200, {}, {}}, "https://passport.bilibili.com/ticket",
      [&](const auto& host, const auto& path, const auto& cookies)
          -> std::optional<Response> {
        assert(host == "passport.bilibili.com");
        assert(path == "/ticket");
        assert(cookies.empty());
        return complete;
      });
  assert(ticketOnly.error.empty());
  assert(ticketOnly.cookies == direct.cookies);

  // Partial poll credentials must survive ticket exchange and every redirect.
  const Response partial{200, {"DedeUserID=123; Path=/"}, {}};
  const auto chained = Resolve(partial, "https://passport.bilibili.com/login/ticket?t=opaque",
      [&](const auto& host, const auto& path, const auto& cookies)
          -> std::optional<Response> {
        ++requests;
        assert(cookies.find("DedeUserID=123") != std::string::npos);
        if (requests == 1) {
          assert(host == "passport.bilibili.com");
          assert(path == "/login/ticket?t=opaque");
          return Response{302, {" SESSDATA = session%2Cvalue== ; HttpOnly"},
                          "//passport-api.bilibili.com/next"};
        }
        assert(requests == 2);
        assert(host == "passport-api.bilibili.com");
        assert(path == "/next");
        assert(cookies.find("SESSDATA=session%2Cvalue==") != std::string::npos);
        return Response{200, {"bili_jct=csrf; Path=/", "sid=optional; Path=/"}, {}};
      });
  assert(chained.error.empty());
  assert(chained.cookies.at("sid") == "optional");
  assert(requests == 2);
  assert(CookieHeader(chained.cookies).find("HttpOnly") == std::string::npos);

  const auto updated = Resolve(partial, "https://passport.bilibili.com/ticket",
      [&](const auto&, const auto&, const auto&) -> std::optional<Response> {
        auto response = complete;
        response.setCookies.push_back("DedeUserID=456; Path=/");
        return response;
      });
  assert(updated.error.empty());
  assert(updated.cookies.at("DedeUserID") == "456");

  // Query parameters are not evidence that a server issued Cookie credentials.
  auto result = Resolve({},
      "https://passport.bilibili.com/ticket?DedeUserID=123&SESSDATA=fake&bili_jct=fake",
      [](const auto&, const auto&, const auto&) -> std::optional<Response> {
        return Response{200, {}, {}};
      });
  assert(result.cookies.empty());
  assert(result.error == "missing cookies: DedeUserID, SESSDATA, bili_jct");
  result = Resolve(partial, "https://passport.bilibili.com/ticket", noRequest);
  assert(result.error == "cookie exchange request failed at passport.bilibili.com");

  // URL safety is enforced before credentials are sent, also after redirects.
  const auto before = requests;
  for (const auto* url : {"http://passport.bilibili.com/t", "https://evil.test/t",
                          "https://passport.bilibili.com.evil.test/t",
                          "https://unrelated.bilibili.com/t",
                          "https://bilibili.com@evil.test/t", "https://bilibili.com:443/t",
                          "https://bilibili.com\\@evil.test/t", ""}) {
    assert(!Resolve(partial, url, noRequest).error.empty());
  }
  assert(requests == before);
  const Response hostOnly{200, {"DedeUserID=123; Path=/",
                                "SESSDATA=secret; Path=/; HttpOnly"}, {}};
  assert(!Resolve(hostOnly, "https://unrelated.bilibili.com/t", noRequest).error.empty());
  assert(requests == before);
  for (const auto* location : {"http://passport.bilibili.com/t", "https://evil.test/t",
                               "//bilibili.com.evil.test/t",
                               "https://unrelated.bilibili.com/t"}) {
    int calls = 0;
    result = Resolve(partial, "https://passport.bilibili.com/ticket",
        [&](const auto&, const auto&, const auto&) -> std::optional<Response> {
          ++calls;
          return Response{302, {"SESSDATA=secret"}, location};
        });
    assert(!result.error.empty());
    assert(result.cookies.empty());
    assert(calls == 1);
    assert(result.error.find("secret") == std::string::npos);
  }
  assert(!AllowedHost("new-passport.bilibili.com"));
  assert(AllowedHost("passport.bilibili.com"));
  assert(AllowedHost("passport-api.bilibili.com"));
  assert(!AllowedHost("notbilibili.com"));

  // Empty cookies and invalid UIDs never replace the saved account.
  for (const auto* uid : {"", "0", "000", "abc", "123x"}) {
    Cookies cookies{{"DedeUserID", uid}, {"SESSDATA", "session"}, {"bili_jct", "csrf"}};
    assert(!Complete(cookies));
  }
  Cookies cookies{{"DedeUserID", "123"}, {"SESSDATA", "session"}, {"bili_jct", ""}};
  assert(!Complete(cookies));
  AddCookie(cookies, "bili_jct=csrf; Path=/");
  assert(Complete(cookies));
  AddCookie(cookies, "SESSDATA=; Max-Age=0");
  assert(!Complete(cookies));
  AddCookie(cookies, "unrelated=ignored");
  assert(cookies.count("unrelated") == 0);
  AddCookie(cookies, "malformed");

  // Reject upstream errors even if their headers happen to contain credentials.
  result = Resolve({}, "https://passport.bilibili.com/ticket",
      [&](const auto&, const auto&, const auto&) -> std::optional<Response> {
        auto error = complete;
        error.status = 403;
        return error;
      });
  assert(result.cookies.empty());
  assert(result.error == "cookie exchange HTTP 403 at passport.bilibili.com");

  int redirects = 0;
  result = Resolve({}, "https://passport.bilibili.com/ticket",
      [&](const auto&, const auto&, const auto&) -> std::optional<Response> {
        ++redirects;
        return Response{302, {}, "/again"};
      });
  assert(result.error == "too many login redirects");
  assert(redirects == MaxRedirects + 1);

  // Match URL joining for relative, root-relative and query-only Location values.
  assert(RedirectUrl("passport.bilibili.com", "/a/b/ticket?old=1", "../done?new=1") ==
         "https://passport.bilibili.com/a/done?new=1");
  assert(RedirectUrl("passport.bilibili.com", "/a/ticket?old=1", "?new=1") ==
         "https://passport.bilibili.com/a/ticket?new=1");
  assert(RedirectUrl("passport.bilibili.com", "/a/ticket", "/done") ==
         "https://passport.bilibili.com/done");
  assert(RedirectUrl("passport.bilibili.com", "/a/ticket", "../") ==
         "https://passport.bilibili.com/");
  assert(RedirectUrl("passport.bilibili.com", "/a/ticket", "") == "");
  std::string host, path;
  assert(ParseHttpsUrl("https://PASSPORT.BILIBILI.COM?ticket=x#ignored", host, path));
  assert(host == "passport.bilibili.com");
  assert(path == "/?ticket=x");
  std::cout << "Bilibili login tests passed\n";
}
