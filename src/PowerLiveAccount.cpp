#include "PowerLiveAccount.hpp"
#include "VoicePlatform.hpp"
#include "Platform.hpp"
#include <cpr/cpr.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/crypto.h>
#include <chrono>
#include <stdexcept>

namespace {
using nlohmann::json;
const std::string issuer = "https://api.powerlive.io";
int64_t Now() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
std::string Base64Url(const unsigned char* data, size_t size) {
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string result; unsigned bits = 0; int count = 0;
  for (size_t i = 0; i < size; ++i) { bits = (bits << 8) | data[i]; count += 8; while (count >= 6) { count -= 6; result += alphabet[(bits >> count) & 63]; } }
  if (count) result += alphabet[(bits << (6 - count)) & 63];
  return result;
}
std::string Random() { unsigned char data[32]; if (RAND_bytes(data, sizeof(data)) != 1) throw std::runtime_error("random"); return Base64Url(data, sizeof(data)); }
std::string Escape(const std::string& value) { cpr::Session session; return session.GetCurlHolder()->urlEncode(value); }
struct Result { json body; long status = 0; };
Result Request(const std::string& url, const std::string& bearer = "", const cpr::Payload* payload = nullptr) {
  std::string body;
  cpr::Header headers{{"Accept", "application/json"}, {"User-Agent", "JPet/1.0"}};
  if (!bearer.empty()) headers["Authorization"] = "Bearer " + bearer;
  const cpr::WriteCallback write{[&](std::string chunk, intptr_t) { if (body.size() + chunk.size() > 512 * 1024) return false; body += chunk; return true; }};
  const auto response = payload ? cpr::Post(cpr::Url{url}, headers, *payload, cpr::Timeout{15000}, cpr::ConnectTimeout{4000}, cpr::Redirect{false}, write) :
    cpr::Get(cpr::Url{url}, headers, cpr::Timeout{15000}, cpr::ConnectTimeout{4000}, cpr::Redirect{false}, write);
  if (response.error.code != cpr::ErrorCode::OK) return {};
  return {json::parse(body, nullptr, false), response.status_code};
}
void Revoke(const std::string& token) {
  if (token.empty()) return;
  const cpr::Payload payload{{"client_id", "jpet-desktop"}, {"token", token}, {"token_type_hint", "refresh_token"}};
  Request(issuer + "/oauth/revoke", "", &payload);
}
}
PowerLiveAccount& PowerLiveAccount::Instance() { static PowerLiveAccount account; return account; }
PowerLiveAccount::PowerLiveAccount() { worker_ = std::thread([this] { Run(); }); }
PowerLiveAccount::~PowerLiveAccount() {
  { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
  wake_.notify_one(); if (worker_.joinable()) worker_.join();
}
void PowerLiveAccount::Configure(const std::string& profile, const std::string& service, int port) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (configured_) return;
  profile_ = profile; service_ = service; redirect_ = "http://127.0.0.1:" + std::to_string(port) + "/oauth/callback";
  configured_ = true; requested_ = true; wake_.notify_one();
}
void PowerLiveAccount::Wake() { requested_ = true; wake_.notify_one(); } // mutex_ held
json PowerLiveAccount::Status() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (configured_ && Now() - attemptAt_ >= 30 && (expires_ < Now() + 90 || quotaAt_ < Now() - 30)) Wake();
  if (!state_.empty() && Now() >= loginExpires_) { state_.clear(); verifier_.clear(); error_ = "登录等待已超时，请重新登录"; }
  return {{"logged_in", !access_.empty() && expires_ > Now()}, {"user", user_}, {"quota", quota_}, {"service_ready", serviceReady_},
    {"pending", !state_.empty()}, {"refreshing", refreshing_}, {"error", error_}};
}
bool PowerLiveAccount::Login(std::string& error) {
  std::lock_guard<std::mutex> operation(operation_);
  std::string url;
  try {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!configured_) { error = "账号服务正在启动，请稍后重试"; return false; }
    state_ = Random(); verifier_ = Random();
    unsigned char digest[SHA256_DIGEST_LENGTH]; SHA256(reinterpret_cast<const unsigned char*>(verifier_.data()), verifier_.size(), digest);
    loginExpires_ = Now() + 600; error_.clear();
    url = issuer + "/oauth/authorize?response_type=code&client_id=jpet-desktop&redirect_uri=" + Escape(redirect_) +
      "&scope=" + Escape("openid profile email offline_access jpet:ai") + "&state=" + state_ + "&nonce=" + Random() +
      "&code_challenge_method=S256&code_challenge=" + Base64Url(digest, sizeof(digest));
  } catch (...) { error = "无法创建安全登录请求"; return false; }
  if (::Platform::OpenWebURL(url, error)) return true;
  std::lock_guard<std::mutex> lock(mutex_); state_.clear(); verifier_.clear(); error_ = error; return false;
}
bool PowerLiveAccount::Callback(const std::string& state, const std::string& code, const std::string& failure, std::string& error) {
  std::lock_guard<std::mutex> operation(operation_);
  std::string verifier, redirect;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.empty() || state.size() != state_.size() || CRYPTO_memcmp(state.data(), state_.data(), state.size()) || Now() >= loginExpires_) {
      error = "登录请求已失效或不匹配，请返回 JPet 重新登录"; return false;
    }
    verifier = verifier_; redirect = redirect_; state_.clear(); verifier_.clear();
  }
  if (!failure.empty() || code.empty() || code.size() > 4096) error = "PowerLive 登录未完成，请重新登录";
  else {
    const cpr::Payload payload{{"client_id", "jpet-desktop"}, {"grant_type", "authorization_code"}, {"code", code}, {"redirect_uri", redirect}, {"code_verifier", verifier}};
    const auto response = Request(issuer + "/oauth/token", "", &payload);
    if (response.status == 200 && Install(response.body, error)) return true;
    if (error.empty()) error = "PowerLive 登录交换失败，请重新登录";
  }
  std::lock_guard<std::mutex> lock(mutex_); error_ = error; return false;
}
bool PowerLiveAccount::Install(const json& tokens, std::string& error) {
  try {
    const auto access = tokens.at("access_token").get<std::string>(), refresh = tokens.at("refresh_token").get<std::string>();
    const int lifetime = tokens.at("expires_in").get<int>();
    if (tokens.value("token_type", std::string{}) != "Bearer" || access.empty() || access.size() > 8192 || refresh.empty() || refresh.size() > 2048 || lifetime <= 0 || lifetime > 3600) throw std::runtime_error("tokens");
    // Persist the rotated credential immediately; never retry an already-used refresh token.
    if (!Voice::SavePowerLiveRefreshToken(profile_, refresh, error)) { Revoke(refresh); return false; }
    const auto account = Request(issuer + "/oauth/userinfo", access);
    if (account.status != 200 || !account.body.is_object() || !account.body.contains("sub")) {
      Revoke(refresh); std::string ignored; Voice::SavePowerLiveRefreshToken(profile_, "", ignored);
      error = "无法验证 PowerLive 账号，请重新登录"; return false;
    }
    const auto me = Request(service_ + "/v1/ai/me", access);
    std::lock_guard<std::mutex> lock(mutex_);
    access_ = access; idToken_ = tokens.value("id_token", std::string{}); expires_ = Now() + lifetime;
    user_ = {{"id", account.body.at("sub")}, {"name", account.body.value("name", account.body.value("preferred_username", std::string{}))}, {"email", account.body.value("email", std::string{})}};
    quota_ = me.status == 200 ? me.body.value("quota", json(nullptr)) : json(nullptr);
    serviceReady_ = me.status == 200 && me.body.value("service_ready", false);
    quotaAt_ = Now(); error_.clear(); return true;
  } catch (...) { error = "PowerLive 返回的登录信息无效，请重新登录"; return false; }
}
bool PowerLiveAccount::Refresh(std::string& error) {
  std::string current;
  { std::lock_guard<std::mutex> lock(mutex_); if (!configured_) return false; current = access_; if (expires_ < Now() + 90) current.clear(); }
  if (current.empty()) {
    const auto refresh = Voice::LoadPowerLiveRefreshToken(profile_, error);
    if (refresh.empty()) return false;
    const cpr::Payload payload{{"client_id", "jpet-desktop"}, {"grant_type", "refresh_token"}, {"refresh_token", refresh}};
    const auto response = Request(issuer + "/oauth/token", "", &payload);
    if (response.status == 200) return Install(response.body, error);
    // A lost token response may have rotated the credential. Require a fresh login
    // instead of replaying it and revoking the token family on the next attempt.
    std::string ignored; Voice::SavePowerLiveRefreshToken(profile_, "", ignored);
    { std::lock_guard<std::mutex> lock(mutex_); access_.clear(); idToken_.clear(); expires_ = 0; user_ = nullptr; quota_ = nullptr; }
    error = "PowerLive 登录已失效，请重新登录"; return false;
  }
  const auto response = Request(service_ + "/v1/ai/me", current);
  std::lock_guard<std::mutex> lock(mutex_); quotaAt_ = Now();
  if (response.status == 200 && response.body.is_object()) {
    quota_ = response.body.value("quota", json(nullptr)); serviceReady_ = response.body.value("service_ready", false); error_.clear(); return true;
  }
  if (response.status == 401) { access_.clear(); expires_ = 0; }
  error = "无法获取 AI 服务额度，请稍后重试"; return false;
}
void PowerLiveAccount::Run() {
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait_for(lock, std::chrono::seconds(30), [&] { return stopping_ || requested_; });
      if (stopping_) return;
      // Keep login fresh even while the settings panel is closed, without doing
      // network I/O on the renderer or reading credentials for signed-out users.
      if (!requested_ && (!configured_ || access_.empty() || expires_ >= Now() + 90)) continue;
      requested_ = false;
    }
    std::lock_guard<std::mutex> operation(operation_);
    { std::lock_guard<std::mutex> lock(mutex_); refreshing_ = true; attemptAt_ = Now(); }
    std::string error; try { Refresh(error); } catch (...) { error = "账号服务暂时不可用，请稍后重试"; }
    { std::lock_guard<std::mutex> lock(mutex_); refreshing_ = false; if (!error.empty()) error_ = error; }
  }
}
std::string PowerLiveAccount::CachedAccessToken(std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!access_.empty() && expires_ > Now() + 60) return access_;
  if (configured_ && Now() - attemptAt_ >= 30) Wake();
  error = refreshing_ || requested_ ? "正在刷新 PowerLive 登录，请稍后再按住快捷键" : "请在设置 → 声音中登录 PowerLive 账号"; return {};
}
std::string PowerLiveAccount::AccessToken(std::string& error) {
  std::lock_guard<std::mutex> operation(operation_);
  { std::lock_guard<std::mutex> lock(mutex_); if (!access_.empty() && expires_ > Now() + 60) return access_; }
  if (!Refresh(error)) { if (error.empty()) error = "请先登录 PowerLive 账号"; return {}; }
  std::lock_guard<std::mutex> lock(mutex_); return access_;
}
bool PowerLiveAccount::Logout(std::string& error) {
  std::lock_guard<std::mutex> operation(operation_);
  const auto refresh = Voice::LoadPowerLiveRefreshToken(profile_, error);
  if (!error.empty() || !Voice::SavePowerLiveRefreshToken(profile_, "", error)) return false;
  std::string hint;
  { std::lock_guard<std::mutex> lock(mutex_); hint = idToken_; access_.clear(); idToken_.clear(); state_.clear(); verifier_.clear(); expires_ = 0; user_ = nullptr; quota_ = nullptr; error_.clear(); requested_ = false; serviceReady_ = false; }
  Revoke(refresh);
  // Also end the browser SSO session so a different account can be chosen next time.
  if (!hint.empty()) { std::string ignored; ::Platform::OpenWebURL(issuer + "/oauth/logout?client_id=jpet-desktop&id_token_hint=" + Escape(hint), ignored); }
  return true;
}
