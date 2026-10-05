#include "VoiceChat.hpp"

#include "AudioManager.hpp"
#include "DataManager.hpp"
#include "LAppDefine.hpp"
#include "LAppPal.hpp"
#include <algorithm>
#include <cstdlib>

namespace {
#ifdef __APPLE__
constexpr const char* Shortcut = "Option";
#else
constexpr const char* Shortcut = "Ctrl";
#endif
}

VoiceChat* VoiceChat::GetInstance() {
  static VoiceChat instance;
  return &instance;
}

VoiceChat::VoiceChat() : platform_(Voice::MakePlatform()), session_({
    [this](const nlohmann::json& event) { platform_->Send(event.dump()); },
    [this](const std::string& pcm) {
      auto* data = DataManager::GetInstance();
      const float volume = data->GetConfig<bool>("audio", "mute", false) ? 0.0f :
          std::clamp(data->GetConfig<int>("audio", "volume", 20), 0, 100) / 100.0f;
      platform_->Play(pcm, volume);
    },
    [this] { return platform_->IsPlaying(); },
    [this] { platform_->StopPlayback(); },
    [this](const std::string& state, const std::string& message) {
      SetState(state, message);
      if (state == "error") { failurePending_ = true; error_ = message; }
    },
    [this](const std::string& reply) {
      std::lock_guard<std::mutex> lock(statusMutex_);
      status_["reply"] = reply;
    }
}) {
  lastActivity_ = stateChangedAt_ = Clock::now();
}

void VoiceChat::SetState(const std::string& state, const std::string& message) {
  std::string text = message;
  if (text.empty()) {
    if (state == "listening") text = std::string("正在听… 松开 ") + Shortcut + " 发送";
    else if (state == "connecting") text = "正在连接千问…";
    else if (state == "thinking") text = "千问正在思考…";
    else if (state == "speaking") text = "千问正在回复…";
    else text = std::string("按住 ") + Shortcut + " 说话";
  }
  {
    std::lock_guard<std::mutex> lock(statusMutex_);
    if (status_["state"] != state || status_["message"] != text) stateChangedAt_ = Clock::now();
    status_["state"] = state;
    status_["message"] = text;
  }
  indicator_ = text;
  indicatorError_ = state == "error";
}

nlohmann::json VoiceChat::Status() {
  std::lock_guard<std::mutex> lock(statusMutex_);
  auto status = status_;
  status["model"] = Voice::Model;
  status["shortcut"] = Shortcut;
  return status;
}

bool VoiceChat::IsBusy() const { return busy_; }

void VoiceChat::Begin() {
  auto* data = DataManager::GetInstance();
  const auto workspace = data->GetConfig<std::string>("voice", "workspace_id", "");
  if (!Voice::ValidWorkspace(workspace) || !data->GetConfig<bool>("voice", "has_api_key", false)) {
    SetState("error", "请先在设置 → 语音对话中填写 API Key 和业务空间 ID");
    waitForRelease_ = true;
    return;
  }
  if (!connected_) {
    std::string error;
    const auto key = Voice::LoadApiKey(LAppPal::WStringToString(LAppDefine::documentPath), error);
    if (!error.empty() || key.empty()) {
      SetState("error", error.empty() ? "未找到 API Key，请在语音对话设置中重新保存" : error);
      waitForRelease_ = true;
      return;
    }
    session_.Reset();
    platform_->Connect(Voice::ConnectionUrl(workspace), key);
    connected_ = true;
    connectedAt_ = Clock::now();
  }
  AudioManager::GetInstance()->Stop();
  session_.BeginInput();
  platform_->StartCapture();
  turnStarted_ = true;
  lastActivity_ = Clock::now();
}

void VoiceChat::Drain() {
  for (const auto& event : platform_->Poll()) {
    if (event.type == Voice::Event::Type::Error) { Fail(event.data); break; }
    try {
      if (event.type == Voice::Event::Type::Microphone) session_.AppendInput(event.data);
      else {
        session_.Receive(nlohmann::json::parse(event.data));
        lastActivity_ = Clock::now();
      }
    } catch (const std::exception&) {
      // Never expose a server payload or a credential in logs/error messages.
      Fail("千问返回了无法处理的语音数据，请重新说话");
      break;
    }
    if (failurePending_) { Fail(error_); break; }
  }
}

void VoiceChat::Close() {
  platform_->StopCapture();
  platform_->Disconnect();
  session_.Reset();
  connected_ = turnStarted_ = failurePending_ = false;
  busy_ = false;
}

void VoiceChat::Fail(const std::string& error) {
  // error can refer to error_, which Close may modify in a future backend.
  const auto message = error;
  Close();
  waitForRelease_ = rawHeld_;
  SetState("error", message);
}

void VoiceChat::Tick(GLFWwindow* window) {
  if (std::getenv("JPET_SMOKE_TEST")) return;
  window_ = window;
  const auto now = Clock::now();
  const bool held = platform_->ShortcutHeld();
  if (resetRequested_.exchange(false)) {
    Close();
    waitForRelease_ = held;
    SetState("idle");
  }
  if (held && !rawHeld_) keyPressedAt_ = now;
  rawHeld_ = held;
  if (!held) {
    if (turnStarted_) {
      platform_->StopCapture();
      Drain();  // Include the microphone's final queued samples before commit.
      session_.EndInput();
      turnStarted_ = false;
      lastActivity_ = now;
    }
    waitForRelease_ = false;
  } else if (!turnStarted_ && !waitForRelease_ && now - keyPressedAt_ >= std::chrono::milliseconds(180)) {
    Begin();
  }
  Drain();
  if (turnStarted_ && now - keyPressedAt_ >= std::chrono::seconds(60)) {
    platform_->StopCapture();
    Drain();
    session_.EndInput();
    turnStarted_ = false;
    waitForRelease_ = true;
  }
  if (connected_ && !session_.Ready() && now - connectedAt_ > std::chrono::seconds(15))
    Fail("连接千问超时，请检查网络、API Key 和业务空间 ID");
  else if (connected_ && session_.Busy() && !session_.Recording() && now - lastActivity_ > std::chrono::seconds(45))
    Fail("千问回复超时，请重新按住快捷键说话");
  else if (connected_ && !session_.Busy() && !platform_->IsPlaying() && now - lastActivity_ > std::chrono::minutes(2)) {
    Close();
    SetState("idle");
  }
  busy_ = session_.Busy() || platform_->IsPlaying();
  const auto state = Status().value("state", std::string{});
  if (state == "speaking" && !busy_) SetState("idle");
  const bool show = busy_ || now - stateChangedAt_ < std::chrono::seconds(indicatorError_ ? 6 : 2);
  platform_->ShowIndicator(window_, show ? indicator_ : "", indicatorError_);
}

void VoiceChat::Stop() {
  Close();
  if (window_) platform_->ShowIndicator(window_, "", false);
  window_ = nullptr;
}
