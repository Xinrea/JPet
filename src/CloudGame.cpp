#include "CloudGame.hpp"
#include "JPetCloudConfig.hpp"
#include "DataManager.hpp"
#include "BuffManager.hpp"
#include "PanelServer.hpp"
#include <httplib.h>
#include <openssl/rand.h>

namespace {
std::string NewId() {
  unsigned char bytes[16];
  if (RAND_bytes(bytes, sizeof(bytes)) != 1) throw std::runtime_error("无法创建同步请求 ID");
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  for (auto b : bytes) { result += hex[b >> 4]; result += hex[b & 15]; }
  return result;
}
}

CloudGame* CloudGame::GetInstance() { static CloudGame instance; return &instance; }
CloudGame::~CloudGame() { Stop(); }
std::string CloudGame::ServiceUrl() { return JPetCloudConfig::ServiceUrl; }
void CloudGame::Start() {
  if (running_.exchange(true)) return;
  worker_ = std::thread(&CloudGame::Run, this);
}
void CloudGame::Stop() {
  if (!running_.exchange(false)) return;
  wake_.notify_all();
  if (worker_.joinable()) worker_.join();
  std::lock_guard<std::mutex> lock(networkMutex_);
  Close();
  SetStatus(false, "已暂停");
}
void CloudGame::Wake(bool takeOver) {
  if (takeOver) takeOver_ = true;
  requested_ = true;
  wake_.notify_all();
}
void CloudGame::SetStatus(bool ready, const std::string& error) {
  bool changed, paused;
  {
    std::lock_guard<std::mutex> lock(statusMutex_);
    changed = ready_ != ready || error_ != error;
    paused = ready_ && !ready;
    ready_ = ready; error_ = error;
  }
  if (paused) DataManager::GetInstance()->PauseCloudView();
  if (changed) PanelServer::GetInstance()->Notify("UPDATE");
}
bool CloudGame::Online() { std::lock_guard<std::mutex> lock(statusMutex_); return ready_; }
nlohmann::json CloudGame::Status() {
  std::lock_guard<std::mutex> lock(statusMutex_);
  return {{"configured", true}, {"online", ready_}, {"error", error_},
    {"heartbeat_seconds", 15}, {"lease_seconds", 30}};
}
void CloudGame::Touch() { if (Online()) ++touches_; }
nlohmann::json CloudGame::Payload() {
  auto bf = BuffManager::GetInstance();
  return {{"uid", uid_}, {"name", name_}, {"session_id", session_}, {"request_id", NewId()},
    {"buffs", bf->GetBuffList()}, {"medal", bf->MedalLevel()}, {"touch_total", touches_.load()}};
}
std::string CloudGame::Request(const std::string& kind, const nlohmann::json& payload, bool* received) {
  if (received) *received = false;
  try {
    const auto split = url_.find('/', url_.find("://") + 3);
    const auto origin = split == std::string::npos ? url_ : url_.substr(0, split);
    auto prefix = split == std::string::npos ? std::string{} : url_.substr(split);
    while (!prefix.empty() && prefix.back() == '/') prefix.pop_back();
    httplib::Client client(origin);
    client.set_connection_timeout(2, 0);
    client.set_read_timeout(4, 0);
    client.set_write_timeout(2, 0);
    client.enable_server_certificate_verification(true);
    auto response = client.Post(prefix + "/v1/" + kind, payload.dump(), "application/json");
    if (!response) return "云端连接中断，游戏已暂停，正在重试";
    auto data = nlohmann::json::parse(response->body);
    if (data.contains("snapshot")) {
      const auto& state = data.at("snapshot");
      if (state.value("uid", std::string{}) != uid_) return "云端返回了不匹配的存档";
      DataManager::GetInstance()->ApplyCloudSnapshot(state);
    }
    if (response->status >= 500) return data.value("error", std::string{"云端服务暂时不可用"});
    if (received) *received = true;
    if (response->status >= 200 && response->status < 300) return {};
    const auto code = data.value("code", std::string{});
    if (code == "SESSION_REPLACED" || code == "SESSION_REQUIRED") opened_ = false;
    if (code == "SESSION_REPLACED" || code == "SESSION_BUSY") SetStatus(false, data.value("error", std::string{"会话已暂停"}));
    return data.value("error", std::string{"云端服务暂时不可用"});
  } catch (const std::exception& e) {
    LAppPal::PrintLog(LogLevel::Warn, "[CloudGame]Request failed: %s", e.what());
    return "云端响应不可用，游戏已暂停，正在重试";
  }
}
bool CloudGame::ReplayPending() {
  if (pending_.is_null() || pending_.empty()) return true;
  bool received = false;
  auto error = Request("command", pending_, &received);
  if (!received) { SetStatus(false, error); return false; }
  pending_ = nullptr;
  DataManager::GetInstance()->SetRaw("cloud.pending", std::string{});
  return true;
}
void CloudGame::Close() {
  if (!opened_ || url_.empty()) return;
  // Settle a possible committed command before sending the terminal close.
  ReplayPending();
  Request("close", Payload());
  opened_ = false;
}
void CloudGame::Disconnect() {
  std::lock_guard<std::mutex> lock(networkMutex_);
  Close();
  uid_.clear(); session_.clear(); touches_ = 0;
  SetStatus(false, "请先登录账号");
}
void CloudGame::Sync() {
  std::lock_guard<std::mutex> lock(networkMutex_);
  auto dm = DataManager::GetInstance();
  const auto endpoint = ServiceUrl();
  auto uid = dm->GetWithDefault("uid", std::string{});
  if (dm->GetWithDefault("cookies", std::string{}).empty()) uid.clear();
  if (uid.empty()) { Close(); SetStatus(false, "请先登录账号"); return; }
  if (uid_ != uid || url_ != endpoint) {
    Close();
    uid_ = uid; url_ = endpoint; session_ = NewId(); touches_ = 0;
    name_ = dm->GetWithDefault("uname", std::string{"用户 "} + uid);
    dm->LoadCloudCache(uid, endpoint);
    const auto saved = nlohmann::json::parse(dm->GetWithDefault("cloud.pending", std::string{}), nullptr, false);
    pending_ = saved.is_object() && saved.value("uid", std::string{}) == uid ? saved : nlohmann::json{};
  }
  name_ = dm->GetWithDefault("uname", std::string{"用户 "} + uid);
  if (!ReplayPending()) return;
  auto payload = Payload();
  const bool takeOver = takeOver_.exchange(false);
  if (takeOver) opened_ = false;
  if (!opened_) {
    payload["take_over"] = takeOver;
    payload["bootstrap"] = dm->ExportCloudBootstrap(uid);
    payload["share"] = !payload["bootstrap"].is_null() && dm->GetWithDefault("data-share", 0) == 1;
  }
  auto error = Request(opened_ ? "heartbeat" : "open", payload);
  if (!error.empty()) { SetStatus(false, error); return; }
  opened_ = true;
  SetStatus(true, "");
}
void CloudGame::Run() {
  while (running_) {
    requested_ = false;
    try { Sync(); } catch (const std::exception& e) {
      LAppPal::PrintLog(LogLevel::Warn, "[CloudGame]Sync failed: %s", e.what());
      SetStatus(false, "同步失败，游戏已暂停，正在重试");
    }
    std::unique_lock<std::mutex> lock(waitMutex_);
    wake_.wait_for(lock, std::chrono::seconds(15), [&] { return !running_ || requested_; });
  }
}
std::string CloudGame::Command(const nlohmann::json& action) {
  std::lock_guard<std::mutex> lock(networkMutex_);
  if (!opened_ || !Online()) return "游戏已暂停，请先连接云端";
  if (uid_ != DataManager::GetInstance()->GetWithDefault("uid", std::string{})) { Wake(); return "正在切换账号，请稍后重试"; }
  if (!ReplayPending()) return "上次操作正在同步，请稍后重试";
  auto payload = Payload(); payload["action"] = action;
  // Persist the exact request before sending, so transport retries are idempotent.
  pending_ = payload;
  DataManager::GetInstance()->SetRaw("cloud.pending", payload.dump());
  bool received = false;
  auto error = Request("command", payload, &received);
  if (!received) {
    SetStatus(false, error); Wake();
    return error;
  }
  pending_ = nullptr;
  DataManager::GetInstance()->SetRaw("cloud.pending", std::string{});
  if (!error.empty()) Wake();
  return error;
}
