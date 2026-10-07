#include "CloudGame.hpp"
#include "JPetCloudConfig.hpp"
#include "DataManager.hpp"
#include "BuffManager.hpp"
#include "PanelServer.hpp"
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
  { std::lock_guard<std::mutex> lock(waitMutex_); wake_.notify_all(); }
  if (worker_.joinable()) worker_.join();
  std::lock_guard<std::mutex> lock(networkMutex_);
  Close();
  SetStatus(false, "已暂停");
}
void CloudGame::Wake(bool takeOver) {
  std::lock_guard<std::mutex> lock(waitMutex_);
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
    {"transport", "websocket"}, {"heartbeat_seconds", 15}, {"lease_seconds", 30}};
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
    if (!socket_) {
      auto endpoint = url_;
      while (!endpoint.empty() && endpoint.back() == '/') endpoint.pop_back();
      if (endpoint.rfind("https://", 0) == 0) endpoint.replace(0, 8, "wss://");
      else if (endpoint.rfind("http://", 0) == 0) endpoint.replace(0, 7, "ws://");
      else throw std::runtime_error("Invalid cloud service URL");
      socket_ = CreateCloudSocket();
      socket_->Connect(endpoint + "/v1/socket?uid=" + uid_, [this] {
        std::lock_guard<std::mutex> lock(waitMutex_);
        incoming_ = true; wake_.notify_all();
      });
    }
    auto message = payload; message["type"] = kind;
    if (!socket_->Send(message.dump())) {
      DropConnection("云端消息发送失败，游戏已暂停，正在重试");
      return "云端消息发送失败，游戏已暂停，正在重试";
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline) {
      CloudSocketEvent event;
      if (!socket_->Receive(event, std::chrono::milliseconds(100))) continue;
      if (event.type == CloudSocketEvent::Type::Error) { DropConnection(event.text); return event.text; }
      const auto data = nlohmann::json::parse(event.text);
      ApplyMessage(data);
      if (data.value("type", std::string{}) != "response" || data.value("request_id", std::string{}) != payload.at("request_id").get<std::string>()) continue;
      const int status = data.at("status").get<int>();
      if (status >= 500) return data.value("error", std::string{"云端服务暂时不可用"});
      if (received) *received = true;
      if (status >= 200 && status < 300) return {};
      const auto code = data.value("code", std::string{});
      if (code == "SESSION_REPLACED" || code == "SESSION_REQUIRED" || code == "SESSION_EXPIRED") opened_ = false;
      if (code == "SESSION_REPLACED" || code == "SESSION_BUSY") SetStatus(false, data.value("error", std::string{"会话已暂停"}));
      return data.value("error", std::string{"云端服务暂时不可用"});
    }
    DropConnection("云端响应超时，游戏已暂停，正在重试");
    return "云端响应超时，游戏已暂停，正在重试";
  } catch (const std::exception& e) {
    LAppPal::PrintLog(LogLevel::Warn, "[CloudGame]Request failed: %s", e.what());
    DropConnection("云端响应不可用，游戏已暂停，正在重试");
    return "云端响应不可用，游戏已暂停，正在重试";
  }
}
void CloudGame::DropConnection(const std::string& error) {
  if (socket_) { socket_->Close(); socket_.reset(); }
  opened_ = false;
  SetStatus(false, error);
}
void CloudGame::ApplyMessage(const nlohmann::json& message) {
  if (message.contains("snapshot")) {
    const auto& state = message.at("snapshot");
    if (state.value("uid", std::string{}) != uid_) throw std::runtime_error("Cloud snapshot UID mismatch");
    if (DataManager::GetInstance()->ApplyCloudSnapshot(state) && !state.value("online", false)) SetStatus(false, "游戏已暂停，正在重新连接");
  }
  if (message.value("type", std::string{}) == "session") {
    opened_ = false;
    SetStatus(false, message.value("error", std::string{"会话已暂停"}));
  }
}
void CloudGame::DrainMessages() {
  CloudSocketEvent event;
  while (socket_ && socket_->Receive(event, std::chrono::milliseconds(0))) {
    if (event.type == CloudSocketEvent::Type::Error) { DropConnection(event.text); break; }
    ApplyMessage(nlohmann::json::parse(event.text));
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
  if (opened_ && !url_.empty()) {
    // Settle a possible committed command before sending the terminal close.
    ReplayPending();
    if (opened_) Request("close", Payload());
  }
  if (socket_) { socket_->Close(); socket_.reset(); }
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
  auto nextSync = std::chrono::steady_clock::now();
  while (running_) {
    try {
      if (requested_.exchange(false) || std::chrono::steady_clock::now() >= nextSync) {
        Sync();
        nextSync = std::chrono::steady_clock::now() + std::chrono::seconds(15);
      }
      std::lock_guard<std::mutex> lock(networkMutex_);
      incoming_ = false;
      DrainMessages();
    } catch (const std::exception& e) {
      LAppPal::PrintLog(LogLevel::Warn, "[CloudGame]Sync failed: %s", e.what());
      std::lock_guard<std::mutex> lock(networkMutex_);
      DropConnection("同步失败，游戏已暂停，正在重试");
    }
    std::unique_lock<std::mutex> lock(waitMutex_);
    wake_.wait_until(lock, nextSync, [&] { return !running_ || requested_ || incoming_; });
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
