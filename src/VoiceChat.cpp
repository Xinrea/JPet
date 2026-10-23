#include "VoiceChat.hpp"

#include "DataManager.hpp"
#include "LAppDefine.hpp"
#include "LAppPal.hpp"
#include "VoiceOnnx.hpp"
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
    }) {
  lastActivity_ = stateChangedAt_ = Clock::now();
}

void VoiceChat::PlayReply(const std::string& pcm) {
  auto* data = DataManager::GetInstance();
  const float volume = data->GetConfig<bool>("audio", "mute", false) ? 0.0f :
      std::clamp(data->GetConfig<int>("audio", "volume", 20), 0, 100) / 100.0f;
  platform_->Play(pcm, volume);
}

void VoiceChat::SubmitTools(const std::vector<Voice::ToolCall>& calls) {
  for (const auto& call : calls) {
    const auto definitions = Voice::ToolDefinitions();
    const bool known = std::any_of(definitions.begin(), definitions.end(), [&](const auto& definition) {
      return definition.at("function").at("name") == call.name;
    });
    LAppPal::PrintLog("[Voice] Tool queued name=%s", known ? call.name.c_str() : "unknown");
  }
  if (!calls.empty()) SetState("tool", Voice::ToolLabel(calls.front().name));
  if (!tools_) tools_ = std::make_unique<Voice::ToolExecutor>(Voice::MakeToolDependencies());
  tools_->Submit(calls);
  lastActivity_ = Clock::now();
}

Voice::SpeechModelStore& VoiceChat::ModelStore() {
  std::call_once(modelStoreOnce_, [this] {
    modelStore_ = std::make_unique<Voice::SpeechModelStore>(
        LAppPal::WStringToString(LAppDefine::documentPath) + "/models/sensevoice-int8-2025-09-09",
        Voice::SpeechModelStore::DefaultMirrors());
  });
  return *modelStore_;
}

bool VoiceChat::SpeechModelAction(const std::string& action, std::string& error) {
  auto& store = ModelStore();
  if (action == "download") store.Download();
  else if (action == "cancel") store.Cancel();
  else if (action == "remove") {
    if (connected_) { error = "请先结束语音对话再删除模型"; return false; }
    std::lock_guard<std::mutex> lock(recognizerMutex_);
    localRecognizer_.reset();
    recognizerError_.clear();
    return store.Remove(error);
  } else { error = "未知的模型操作"; return false; }
  return true;
}

bool VoiceChat::StartPipeline(const std::string& workspace, const std::string& key) {
  auto* data = DataManager::GetInstance();
  const auto settings = data->GetVoiceSettings();
  const auto modelDirectory = ModelStore().Installed();
  if (modelDirectory.empty()) {
    SetState("error", "本机语音识别模型尚未下载，请在对话 → 语音对话设置中下载");
    return false;
  }
  const auto profile = LAppPal::WStringToString(LAppDefine::documentPath);
  Voice::CascadeConfig config;
  config.workspace = workspace;
  config.apiKey = key;
  config.chatModel = settings.at("chat_model");
  const std::string baseUrl = settings.at("chat_base_url");
  config.bailianChat = baseUrl.empty();
  config.chatUrl = Voice::ChatCompletionsUrl(baseUrl, workspace);
  config.chatKey = key;
  if (!config.bailianChat) {
    std::string error;
    config.chatKey = Voice::LoadChatApiKey(profile, error);
    if (!error.empty() || config.chatKey.empty()) {
      SetState("error", error.empty() ? "请在语音对话设置中填写自定义大模型的 API Key" : error);
      return false;
    }
  }
  config.ttsModel = settings.at("tts_model");
  config.ttsVoice = settings.at("tts_voice");

  // Hands-free turns need the local models; without them the shortcut still
  // works as push-to-talk (press to speak, press or release to send).
  std::unique_ptr<Voice::TurnAnalyzer> analyzer;
  if (!vad_ && turnModelError_.empty()) {
    const auto directory = LAppPal::WStringToString(LAppDefine::execPath) + "resources/models/";
    auto vadModel = Voice::OnnxModel::Load(directory + "silero_vad.onnx", turnModelError_);
    auto turnModel = vadModel ? Voice::OnnxModel::Load(directory + "smart-turn-v3.2-cpu.onnx", turnModelError_) : nullptr;
    if (vadModel && turnModel) {
      vad_ = std::make_unique<Voice::SileroVad>(std::move(vadModel));
      smartTurn_ = std::make_unique<Voice::SmartTurn>(std::move(turnModel));
    } else LAppPal::PrintLog(LogLevel::Error, "[Voice] Local turn detection unavailable: %s", turnModelError_.c_str());
  }
  if (vad_) {
    vad_->Reset();
    analyzer = std::make_unique<Voice::TurnAnalyzer>(std::make_unique<Voice::TurnDetector>(
        [this](const float* frame) { return vad_->Probability(frame); },
        [this](const std::vector<int16_t>& audio) { return smartTurn_->Probability(audio); }));
  }
  Voice::Pipeline::Callbacks callbacks{
    [this](int channel, const std::string& url, const std::string& apiKey) { platform_->Connect(channel, url, apiKey); },
    [this](int channel) { platform_->Disconnect(channel); },
    [this](int channel, const nlohmann::json& message) { platform_->Send(channel, message.dump()); },
    [this](const std::string& pcm) { PlayReply(pcm); },
    [this] { return platform_->IsPlaying(); },
    [this] { return platform_->PlayedSamples(); },
    [this] { platform_->StopPlayback(); },
    [this](const std::string& state, const std::string& message) { SetState(state, message); },
    [this](const std::string& reply) {
      std::lock_guard<std::mutex> lock(statusMutex_);
      status_["reply"] = reply;
    },
    [this](const std::vector<Voice::ToolCall>& calls) { SubmitTools(calls); },
    [this](const Voice::ConversationEvent& event) { history_.Apply(event); },
    [this] { if (tools_) tools_->Cancel(); Voice::CancelSettingsTools(); },
    [](const std::string& line) { LAppPal::PrintLog("%s", line.c_str()); }};
  pipeline_ = std::make_unique<Voice::Pipeline>(std::move(config), std::move(callbacks), Voice::ToolDefinitions(),
      std::move(analyzer), std::make_unique<Voice::ChatClient>(), [this, modelDirectory](const std::string& pcm) {
    std::lock_guard<std::mutex> lock(recognizerMutex_);
    if (!localRecognizer_ && recognizerError_.empty()) {
      const auto start = Clock::now();
      localRecognizer_ = Voice::LocalRecognizer::Load(modelDirectory, recognizerError_);
      LAppPal::PrintLog("[Voice] Local recognizer load ok=%d ms=%lld", localRecognizer_ ? 1 : 0,
          static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count()));
    }
    return localRecognizer_ ? localRecognizer_->Transcribe(pcm) : std::string{};
  });
  LAppPal::PrintLog("[Voice] Connect chat_model=%s custom_chat=%d tts=%s hands_free=%d",
      settings.at("chat_model").get<std::string>().c_str(), baseUrl.empty() ? 0 : 1,
      settings.at("tts_model").get<std::string>().c_str(), pipeline_->HandsFree() ? 1 : 0);
  if (!pipeline_->HandsFree() && !turnModelError_.empty())
    SetState("connecting", turnModelError_ + "；快捷键改为按一下开始、再按一下发送");
  return true;
}

void VoiceChat::SetState(const std::string& state, const std::string& message) {
  const bool holdMode = DataManager::GetInstance()->GetConfig<std::string>("voice", "input_mode", "toggle") == "hold";
  std::string text = message;
  if (text.empty()) {
    if (state == "listening") text = "正在听…";
    else if (state == "connecting") text = "正在呼唤轴伊…";
    else if (state == "thinking") text = "轴伊正在思考…";
    else if (state == "speaking") text = "轴伊正在回复…";
    else text = std::string(holdMode ? "按住 " : "按 ") + Shortcut + " 开启麦克风";
  }
  const bool microphoneOn = Recording();
  if (microphoneOn && state != "error") {
    if (holdMode) text += std::string(" 松开 ") + Shortcut + " 后回复";
    else text += std::string(" 麦克风已开启，再按 ") + Shortcut + " 关闭";
  }
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
  const auto settings = DataManager::GetInstance()->GetVoiceSettings();
  const auto speechModel = ModelStore().Status();
  std::lock_guard<std::mutex> lock(statusMutex_);
  auto status = status_;
  status["model"] = settings.at("chat_model");
  status["speech_model"] = speechModel;
  status["shortcut"] = Shortcut;
  status["input_mode"] = settings.at("input_mode");
  status["available_tools"] = nlohmann::json::array();
  for (const auto& definition : Voice::ToolDefinitions()) status["available_tools"].push_back(definition["function"]["name"]);
  return status;
}

bool VoiceChat::IsBusy() const { return busy_; }

void VoiceChat::Begin() {
  auto* data = DataManager::GetInstance();
  const auto workspace = data->GetConfig<std::string>("voice", "workspace_id", "");
  if (!Voice::ValidWorkspace(workspace) || !data->GetConfig<bool>("voice", "has_api_key", false)) {
    SetState("error", "请先在对话 → 语音对话设置中填写 API Key 和业务空间 ID");
    shortcut_.BlockUntilRelease();
    return;
  }
  if (!connected_) {
    std::string error;
    const auto key = Voice::LoadApiKey(LAppPal::WStringToString(LAppDefine::documentPath), error);
    if (!error.empty() || key.empty()) {
      SetState("error", error.empty() ? "未找到 API Key，请在语音对话设置中重新保存" : error);
      shortcut_.BlockUntilRelease();
      return;
    }
    if (!StartPipeline(workspace, key)) {
      shortcut_.BlockUntilRelease();
      return;
    }
    connected_ = true;
  }
  if (!tools_) tools_ = std::make_unique<Voice::ToolExecutor>(Voice::MakeToolDependencies());
  capturedBytes_ = 0;
  capturedEnergy_ = 0;
  capturedPeak_ = 0;
  pipeline_->BeginInput(data->GetConfig<std::string>("voice", "input_mode", "toggle") == "hold");
  platform_->StartCapture();
  microphoneEnabled_ = true;
  lastActivity_ = Clock::now();
}

void VoiceChat::End() {
  platform_->StopCapture();
  Drain();  // Include the microphone's final queued samples before closing input.
  if (Recording()) {
    const double rms = capturedBytes_ ? std::sqrt(capturedEnergy_ / (capturedBytes_ / 2)) : 0;
    LAppPal::PrintLog("[Voice] Capture end bytes=%zu audio_ms=%.1f rms=%.1f peak=%d",
        capturedBytes_, capturedBytes_ * 1000.0 / Voice::InputBytesPerSecond, rms, capturedPeak_);
    pipeline_->EndInput();
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
    // The pipeline recovers from its own synthesis connection.
    if (event.channel != Voice::LocalChannel) {
      if (pipeline_) pipeline_->Receive(event);
      lastActivity_ = Clock::now();
      continue;
    }
    if (event.type == Voice::Event::Type::Error) { Fail(event.data); break; }
    if (event.type != Voice::Event::Type::Microphone || !pipeline_) continue;
    if (event.data.size() % 2) { Fail("轴伊没能听清这段语音，请重新说话"); break; }
    if (Recording()) {
      capturedBytes_ += event.data.size();
      for (size_t i = 0; i < event.data.size(); i += 2) {
        const uint16_t raw = static_cast<unsigned char>(event.data[i]) |
            (static_cast<uint16_t>(static_cast<unsigned char>(event.data[i + 1])) << 8);
        const int sample = static_cast<int16_t>(raw);
        capturedEnergy_ += double(sample) * sample;
        capturedPeak_ = std::max(capturedPeak_, std::abs(sample));
      }
    }
    pipeline_->AppendInput(event.data);
  }
}

void VoiceChat::Close(bool failed) {
  if (tools_) tools_->Cancel();
  Voice::CancelSettingsTools();
  platform_->StopCapture();
  platform_->DisconnectAll();
  if (pipeline_) pipeline_->Reset(failed);
  pipeline_.reset();  // Joins its recognition worker before the model can go.
  {
    // SenseVoice holds about half a gigabyte; reload it (about a second) next time.
    std::lock_guard<std::mutex> lock(recognizerMutex_);
    localRecognizer_.reset();
    recognizerError_.clear();
  }
  connected_ = microphoneEnabled_ = false;
  busy_ = false;
}

void VoiceChat::Fail(const std::string& error) {
  // error can refer to error_, which Close may modify in a future backend.
  const auto message = error;
  LAppPal::PrintLog(LogLevel::Error, "[Voice] Failure: %s", message.c_str());
  Close(true);
  shortcut_.BlockUntilRelease();
  SetState("error", message);
}

void VoiceChat::Tick(GLFWwindow* window) {
  if (std::getenv("JPET_SMOKE_TEST")) return;
  window_ = window;
  const auto now = Clock::now();
  const bool held = platform_->ShortcutHeld();
  if (resetRequested_.exchange(false)) {
    Close();
    shortcut_.BlockUntilRelease(held);
    SetState("idle");
  }
  if (historyClearRequested_.exchange(false)) {
    // Messages from the old session may have arrived before Close().
    history_.Clear();
    std::lock_guard<std::mutex> lock(statusMutex_);
    status_["reply"] = "";
    status_.erase("last_tool");
  }
  const bool holdMode = DataManager::GetInstance()->GetConfig<std::string>("voice", "input_mode", "toggle") == "hold";
  const auto action = shortcut_.Update(held, holdMode, microphoneEnabled_);
  if (action == Voice::ShortcutControl::Action::Begin) Begin();
  else if (action == Voice::ShortcutControl::Action::End) End();
  Drain();
  Voice::DrainSettingsTools();
  if (tools_) for (const auto& result : tools_->Poll()) {
    LAppPal::PrintLog("[Voice] Tool completed ok=%d code=%s output_bytes=%zu",
        result.value.value("ok", false), DiagnosticCode(result.value, "code").c_str(), result.value.dump().size());
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
    if (pipeline_) pipeline_->CompleteTool(result.call.id, result.value);
    lastActivity_ = Clock::now();
  }
  if (pipeline_ && pipeline_->Tick()) lastActivity_ = Clock::now();
  if (connected_ && pipeline_->WaitingForReply() && now - lastActivity_ > std::chrono::seconds(45))
    Fail("轴伊回复超时，请按快捷键重新开启麦克风");
  else if (connected_ && !pipeline_->Busy() && !platform_->IsPlaying() && now - lastActivity_ > std::chrono::minutes(2)) {
    Close();
    SetState("idle");
  }
  busy_ = (pipeline_ && pipeline_->Busy()) || platform_->IsPlaying();
  const auto state = Status().value("state", std::string{});
  if (state == "speaking" && !(pipeline_ && pipeline_->WaitingForReply()) && !platform_->IsPlaying())
    SetState(Recording() ? "listening" : "idle");
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
