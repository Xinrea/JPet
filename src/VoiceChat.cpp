#include "PowerLiveAccount.hpp"
#include "CloudGame.hpp"
#include "VoiceChat.hpp"

#include "DataManager.hpp"
#include "LAppDefine.hpp"
#include "LAppPal.hpp"
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <stdexcept>

namespace {
#ifdef __APPLE__
constexpr const char* Shortcut = "Option";
#else
constexpr const char* Shortcut = "Ctrl";
#endif

std::string DiagnosticCode(const nlohmann::json& object, const char* field) {
  const auto it = object.find(field);
  if (it == object.end() || !it->is_string()) return "unknown";
  const auto& value = it->get_ref<const std::string&>();
  if (value.empty() || value.size() > 96 || !std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
      })) return "unknown";
  return value;
}
}

VoiceChat* VoiceChat::GetInstance() {
  static VoiceChat instance;
  return &instance;
}

VoiceChat::VoiceChat() : platform_(Voice::MakePlatform()),
    tools_(std::make_unique<Voice::ToolExecutor>(Voice::MakeToolDependencies())),
    history_(DataManager::GetInstance()->LoadVoiceHistory(), [](const nlohmann::json& history) {
      DataManager::GetInstance()->SaveVoiceHistory(history);
    }), session_({
    [this](const nlohmann::json& event) {
      const auto type = event.value("type", std::string{});
      if (type == "input_audio_buffer.append") {
        const auto& audio = event.at("audio").get_ref<const std::string&>();
        sentBytes_ += audio.size() / 4 * 3 - (audio.back() == '=') -
            (audio[audio.size() - 2] == '=');
        ++sentChunks_;
      } else {
        LAppPal::PrintLog("[Voice] Send type=%s event_id=%s", type.c_str(),
            event.at("event_id").get_ref<const std::string&>().c_str());
      }
      platform_->Send(event.dump());
    },
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
    },
    [this](const std::vector<Voice::ToolCall>& calls) {
      for (const auto& call : calls) {
        const auto definitions = Voice::ToolDefinitions();
        const bool known = std::any_of(definitions.begin(), definitions.end(), [&](const auto& definition) {
          return definition.at("function").at("name") == call.name;
        });
        LAppPal::PrintLog("[Voice] Tool queued name=%s", known ? call.name.c_str() : "unknown");
      }
      if (!calls.empty()) SetState("tool", Voice::ToolLabel(calls.front().name));
      tools_->Submit(calls);
      lastActivity_ = Clock::now();
    },
    [this](const Voice::ConversationEvent& event) { history_.Apply(event); },
    [this] { if (tools_) tools_->Cancel(); Voice::CancelSettingsTools(); }
}, Voice::ToolDefinitions()) {
  lastActivity_ = stateChangedAt_ = Clock::now();
}

void VoiceChat::SetState(const std::string& state, const std::string& message) {
  std::string text = message;
  if (text.empty()) {
    if (state == "listening") text = "正在听…";
    else if (state == "connecting") text = "正在连接千问…";
    else if (state == "thinking") text = "千问正在思考…";
    else if (state == "speaking") text = "千问正在回复…";
    else text = std::string("按 ") + Shortcut + " 开启麦克风";
  }
  const bool microphoneOn = session_.Recording();
  if (microphoneOn && state != "error") text += std::string(" 麦克风已开启，再按 ") + Shortcut + " 关闭";
  {
    std::lock_guard<std::mutex> lock(statusMutex_);
    if (status_["state"] != state || status_["message"] != text) {
      stateChangedAt_ = Clock::now();
      LAppPal::PrintLog("[Voice] State=%s", state.c_str());
    }
    status_["state"] = state;
    status_["message"] = text;
    status_["microphone_on"] = microphoneOn;
  }
  indicator_ = text;
  indicatorError_ = state == "error";
}

nlohmann::json VoiceChat::Status() {
  std::lock_guard<std::mutex> lock(statusMutex_);
  auto status = status_;
  status["model"] = Voice::Model;
  status["shortcut"] = Shortcut;
  status["available_tools"] = nlohmann::json::array();
  for (const auto& definition : Voice::ToolDefinitions()) status["available_tools"].push_back(definition["function"]["name"]);
  return status;
}

bool VoiceChat::IsBusy() const { return busy_; }

void VoiceChat::Begin() {
  auto* data = DataManager::GetInstance();
  const auto workspace = data->GetConfig<std::string>("voice", "workspace_id", "");
  const bool server = data->GetConfig<std::string>("voice", "provider", "custom") == "jpet";
  if (!server && (!Voice::ValidWorkspace(workspace) || !data->GetConfig<bool>("voice", "has_api_key", false))) {
    SetState("error", "请先在对话 → 语音对话设置中填写 API Key 和业务空间 ID");
    waitForRelease_ = true;
    return;
  }
  if (!connected_) {
    std::string error;
    const auto key = server ? PowerLiveAccount::Instance().CachedAccessToken(error) : Voice::LoadApiKey(LAppPal::WStringToString(LAppDefine::documentPath), error);
    if (!error.empty() || key.empty()) {
      SetState("error", error.empty() ? "未找到 API Key，请在语音对话设置中重新保存" : error);
      waitForRelease_ = true;
      return;
    }
    session_.Reset();
    LAppPal::PrintLog("[Voice] Connect model=%s", Voice::Model);
    auto url = server ? CloudGame::ServiceUrl() + "/v1/ai/realtime" : Voice::ConnectionUrl(workspace);
    if (server && url.find("https:") == 0) url.replace(0, 5, "wss");
    else if (server && url.find("http:") == 0) url.replace(0, 4, "ws");
    platform_->Connect(url, key);
    connected_ = true;
    connectedAt_ = Clock::now();
  }
  if (!tools_) tools_ = std::make_unique<Voice::ToolExecutor>(Voice::MakeToolDependencies());
  capturedBytes_ = sentBytes_ = sentChunks_ = 0;
  capturedEnergy_ = 0;
  capturedPeak_ = 0;
  session_.BeginInput();
  platform_->StartCapture();
  microphoneEnabled_ = true;
  lastActivity_ = Clock::now();
}

void VoiceChat::End() {
  platform_->StopCapture();
  Drain();  // Include the microphone's final queued samples before closing input.
  if (session_.Recording()) {
    const double rms = capturedBytes_ ? std::sqrt(capturedEnergy_ / (capturedBytes_ / 2)) : 0;
    LAppPal::PrintLog("[Voice] Capture end bytes=%zu audio_ms=%.1f rms=%.1f peak=%d",
        capturedBytes_, capturedBytes_ * 1000.0 / Voice::InputBytesPerSecond, rms, capturedPeak_);
    session_.EndInput();
    LAppPal::PrintLog("[Voice] Upload queued chunks=%zu bytes=%zu audio_ms=%.1f",
        sentChunks_, sentBytes_, sentBytes_ * 1000.0 / Voice::InputBytesPerSecond);
  }
  microphoneEnabled_ = false;
  lastActivity_ = Clock::now();
}

void VoiceChat::Drain() {
  for (const auto& event : platform_->Poll()) {
    if (event.type == Voice::Event::Type::Diagnostic) {
      LAppPal::PrintLog("%s", event.data.c_str());
      continue;
    }
    if (event.type == Voice::Event::Type::Error) { Fail(event.data); break; }
    try {
      if (event.type == Voice::Event::Type::Microphone) {
        if (event.data.size() % 2) throw std::invalid_argument("Invalid microphone PCM");
        if (session_.Recording()) {
          capturedBytes_ += event.data.size();
          for (size_t i = 0; i < event.data.size(); i += 2) {
            const uint16_t raw = static_cast<unsigned char>(event.data[i]) |
                (static_cast<uint16_t>(static_cast<unsigned char>(event.data[i + 1])) << 8);
            const int sample = static_cast<int16_t>(raw);
            capturedEnergy_ += double(sample) * sample;
            capturedPeak_ = std::max(capturedPeak_, std::abs(sample));
          }
        }
        session_.AppendInput(event.data);
      }
      else {
        const auto message = nlohmann::json::parse(event.data);
        const auto type = DiagnosticCode(message, "type");
        if (type == "session.created" || type == "session.updated" ||
            type == "input_audio_buffer.committed" || type == "response.created" ||
            type == "input_audio_buffer.speech_started" || type == "input_audio_buffer.speech_stopped")
          LAppPal::PrintLog("[Voice] Receive type=%s", type.c_str());
        else if (type == "response.done")
          LAppPal::PrintLog("[Voice] Receive type=response.done status=%s",
              DiagnosticCode(message.at("response"), "status").c_str());
        else if (type == "error")
          LAppPal::PrintLog("[Voice] Server error code=%s", DiagnosticCode(message.at("error"), "code").c_str());
        session_.Receive(message);
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

void VoiceChat::Close(bool failed) {
  if (tools_) tools_->Cancel();
  Voice::CancelSettingsTools();
  platform_->StopCapture();
  platform_->Disconnect();
  session_.Reset(failed);
  connected_ = microphoneEnabled_ = failurePending_ = false;
  busy_ = false;
}

void VoiceChat::Fail(const std::string& error) {
  // error can refer to error_, which Close may modify in a future backend.
  const auto message = error;
  LAppPal::PrintLog(LogLevel::Error, "[Voice] Failure: %s", message.c_str());
  Close(true);
  waitForRelease_ = rawHeld_;
  SetState("error", message);
}

void VoiceChat::Tick(GLFWwindow* window) {
  if (std::getenv("JPET_SMOKE_TEST")) return;
  window_ = window;
  const auto now = Clock::now();
  const bool held = platform_->ShortcutHeld();
  const bool pressed = held && !rawHeld_;
  rawHeld_ = held;
  if (resetRequested_.exchange(false)) {
    Close();
    waitForRelease_ = held;
    SetState("idle");
  }
  if (!held) waitForRelease_ = false;
  // Only the up-to-down edge toggles capture. Holding or releasing the key
  // leaves the microphone in its current state.
  if (pressed && !waitForRelease_) {
    if (microphoneEnabled_) End();
    else Begin();
  }
  Drain();
  Voice::DrainSettingsTools();
  if (tools_) for (const auto& result : tools_->Poll()) {
    LAppPal::PrintLog("[Voice] Tool completed ok=%d", result.value.value("ok", false));
    {
      std::lock_guard<std::mutex> lock(statusMutex_);
      nlohmann::json visible = {{"ok", result.value.value("ok", false)}};
      if (result.call.name == "view_desktop") visible["label"] = "查看桌面";
      else if (result.call.name.compare(0, 4, "get_") == 0) visible["label"] = "查询游戏";
      else if (result.call.name == "game_action") visible["label"] = "游戏操作";
      else if (result.call.name == "jpet_settings") visible["label"] = "JPet 设置";
      else if (result.call.name == "web_search") visible["label"] = "网页搜索";
      else if (result.call.name == "bilibili_search") visible["label"] = "B站搜索";
      else if (result.call.name == "open_url") visible["label"] = "打开网页";
      else visible["label"] = "工具";
      for (const auto* key : {"error", "sources", "results", "url"})
        if (result.value.contains(key)) visible[key] = result.value[key];
      status_["last_tool"] = std::move(visible);
    }
    session_.CompleteTool(result.call.id, result.value);
    lastActivity_ = Clock::now();
  }
  if (connected_ && !session_.Ready() && now - connectedAt_ > std::chrono::seconds(15))
    Fail("连接千问超时，请检查网络、API Key 和业务空间 ID");
  else if (connected_ && session_.WaitingForReply() && now - lastActivity_ > std::chrono::seconds(45))
    Fail("千问回复超时，请按快捷键重新开启麦克风");
  else if (connected_ && !session_.Busy() && !platform_->IsPlaying() && now - lastActivity_ > std::chrono::minutes(2)) {
    Close();
    SetState("idle");
  }
  busy_ = session_.Busy() || platform_->IsPlaying();
  const auto state = Status().value("state", std::string{});
  if (state == "speaking" && !session_.WaitingForReply() && !platform_->IsPlaying())
    SetState(session_.Recording() ? "listening" : "idle");
  const bool show = busy_ || now - stateChangedAt_ < std::chrono::seconds(indicatorError_ ? 6 : 2);
  platform_->ShowIndicator(window_, show ? indicator_ : "", indicatorError_);
}

void VoiceChat::Stop() {
  Close();
  Voice::StopSettingsTools();
  // Join before DataManager/platform teardown; no worker may outlive game data.
  tools_.reset();
  if (window_) platform_->ShowIndicator(window_, "", false);
  window_ = nullptr;
}
