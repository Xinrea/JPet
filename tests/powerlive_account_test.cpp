#include "PowerLiveAccount.hpp"
#include "VoicePlatform.hpp"
#include "Platform.hpp"
#include <iostream>
#include <stdexcept>
#include <cpr/cpr.h>
std::string launched;
namespace Platform { bool OpenWebURL(const std::string& url, std::string&) { launched = url; return true; } }
namespace Voice {
bool SavePowerLiveRefreshToken(const std::string&, const std::string&, std::string&) { return true; }
std::string LoadPowerLiveRefreshToken(const std::string&, std::string&) { return {}; }
}
void Require(bool value) { if (!value) throw std::runtime_error("OAuth safety check failed"); }
std::string Parameter(const std::string& name) {
  const auto begin = launched.find(name + "="); Require(begin != std::string::npos);
  const auto start = begin + name.size() + 1, end = launched.find('&', start);
  return launched.substr(start, end == std::string::npos ? end : end - start);
}
int main() {
  auto& account = PowerLiveAccount::Instance();
  account.Configure("isolated-test", "https://s.jpet.powerlive.io", 49000);
  std::string error;
  Require(account.Login(error));
  Require(launched.find("https://api.powerlive.io/oauth/authorize?") == 0);
  Require(Parameter("redirect_uri") == "http%3A%2F%2F127.0.0.1%3A49000%2Foauth%2Fcallback");
  Require(Parameter("code_challenge_method") == "S256");
  Require(Parameter("code_challenge").size() == 43);
  Require(Parameter("nonce").size() == 43);
  const auto first = Parameter("state"); Require(first.size() == 43);
  Require(!account.Callback("wrong-state", "", "", error));
  Require(account.Status().at("pending"));
  Require(account.Login(error));
  const auto second = Parameter("state"); Require(first != second);
  Require(!account.Callback(first, "", "", error));
  Require(account.Status().at("pending"));
  Require(!account.Callback(second, "", "access_denied", error));
  Require(!account.Status().at("pending"));
  Require(!account.Callback(second, "", "", error));
  const auto status = account.Status();
  Require(!status.contains("access_token") && !status.contains("refresh_token") && !status.contains("id_token"));
  Require(!status.at("logged_in"));
  std::cout << "PASS PKCE, random state/nonce, loopback callback binding, stale callback/replay rejection and secret redaction\n";
}
