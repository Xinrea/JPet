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

// Replies follow JPet's volume, and 设置 → 静音 silences them too.
float ReplyVolume() {
  auto* data = DataManager::GetInstance();
  return data->GetConfig<bool>("audio", "mute", false) ? 0.0f :
      std::clamp(data->GetConfig<int>("audio", "volume", 20), 0, 100) / 100.0f;
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

void VoiceChat::PlayReply(const std::string& pcm) { platform_->Play(pcm, ReplyVolume()); }

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

std::vector<Voice::SpeechModelStore*> VoiceChat::ModelStores() {
  std::call_once(modelStoreOnce_, [this] {
    const auto models = LAppPal::WStringToString(LAppDefine::documentPath) + "/models/";
    recognitionModel_ = std::make_unique<Voice::SpeechModelStore>(models + "sensevoice-int8-2025-09-09",
        Voice::SpeechModelStore::DefaultMirrors());
    synthesisModel_ = std::make_unique<Voice::SpeechModelStore>(models + "qwen3-tts-0.6b-base-q8_0",
        Voice::SpeechModelStore::SynthesisMirrors(), Voice::SpeechModelStore::SynthesisFiles());
  });
  return {recognitionModel_.get(), synthesisModel_.get()};
}

nlohmann::json VoiceChat::SpeechModelStatus() {
  std::vector<nlohmann::json> statuses;
  for (auto* store : ModelStores()) statuses.push_back(store->Status());
  return Voice::SpeechModelStore::Combine(statuses);
}

bool VoiceChat::SpeechModelAction(const std::string& action, std::string& error) {
  const auto stores = ModelStores();
  if (action == "download") for (auto* store : stores) store->Download();
  else if (action == "cancel") for (auto* store : stores) store->Cancel();
  else if (action == "remove") {
    if (connected_ && voice_) { error = "请先结束语音对话再删除模型"; return false; }
    std::lock_guard<std::mutex> lock(recognizerMutex_);
    localRecognizer_.reset();
    recognizerError_.clear();
    bool removed = true;
    for (auto* store : stores) removed = store->Remove(error) && removed;
    return removed;
  } else { error = "未知的模型操作"; return false; }
  return true;
}

bool VoiceChat::StartPipeline(const std::string& workspace, const std::string& key, bool voice) {
  auto* data = DataManager::GetInstance();
  const auto settings = data->GetVoiceSettings();
  std::string modelDirectory, synthesisDirectory;
  Voice::VoiceReference reference;
  if (voice) {
    const auto stores = ModelStores();
    modelDirectory = stores[0]->Installed();
    synthesisDirectory = stores[1]->Installed();
    if (modelDirectory.empty() || synthesisDirectory.empty()) {
      SetState("error", "本机语音模型尚未下载，请在对话 → 语音对话设置中下载");
      return false;
    }
    std::string synthesisError;
    if (!Voice::LocalSynthesisAvailable(synthesisError) || !Voice::LoadVoiceReference(
            LAppPal::WStringToString(LAppDefine::execPath) + "resources/models/joi-voice.json", reference, synthesisError)) {
      SetState("error", synthesisError);
      return false;
    }
  }
  const auto profile = LAppPal::WStringToString(LAppDefine::documentPath);
  Voice::CascadeConfig config;
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

  // Hands-free turns need the local models; without them the shortcut still
  // works as push-to-talk (press to speak, press or release to send).
  std::unique_ptr<Voice::TurnAnalyzer> analyzer;
  if (voice && !vad_ && turnModelError_.empty()) {
    const auto directory = LAppPal::WStringToString(LAppDefine::execPath) + "resources/models/";
    auto vadModel = Voice::OnnxModel::Load(directory + "silero_vad.onnx", turnModelError_);
    auto turnModel = vadModel ? Voice::OnnxModel::Load(directory + "smart-turn-v3.2-cpu.onnx", turnModelError_) : nullptr;
    if (vadModel && turnModel) {
      vad_ = std::make_unique<Voice::SileroVad>(std::move(vadModel));
      smartTurn_ = std::make_unique<Voice::SmartTurn>(std::move(turnModel));
    } else LAppPal::PrintLog(LogLevel::Error, "[Voice] Local turn detection unavailable: %s", turnModelError_.c_str());
  }
  if (voice && vad_) {
    vad_->Reset();
    analyzer = std::make_unique<Voice::TurnAnalyzer>(std::make_unique<Voice::TurnDetector>(
        [this](const float* frame) { return vad_->Probability(frame); },
        [this](const std::vector<int16_t>& audio) { return smartTurn_->Probability(audio); }));
  }
  // The synthesis model starts loading now, while the user is still speaking.
  std::unique_ptr<Voice::Synthesizer> synthesizer;
  if (voice) {
    const auto& files = Voice::SpeechModelStore::SynthesisFiles();
    synthesizer = Voice::MakeLocalSynthesizer(
        {synthesisDirectory + "/" + files[0].name, synthesisDirectory + "/" + files[1].name}, std::move(reference),
        [](const std::string& line) { LAppPal::PrintLog("%s", line.c_str()); });
  }
  Voice::Pipeline::Callbacks callbacks{
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
      std::move(analyzer), std::make_unique<Voice::ChatClient>(), std::move(synthesizer),
      [this, modelDirectory](const std::string& pcm) {
    std::lock_guard<std::mutex> lock(recognizerMutex_);
    if (!localRecognizer_ && recognizerError_.empty()) {
      const auto start = Clock::now();
      localRecognizer_ = Voice::LocalRecognizer::Load(modelDirectory, recognizerError_);
      LAppPal::PrintLog("[Voice] Local recognizer load ok=%d ms=%lld", localRecognizer_ ? 1 : 0,
          static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count()));
    }
    return localRecognizer_ ? localRecognizer_->Transcribe(pcm) : std::string{};
  });
  pipeline_->SetContext(context_);
  LAppPal::PrintLog("[Voice] Connect chat_model=%s custom_chat=%d voice=%d hands_free=%d context=%zu",
      settings.at("chat_model").get<std::string>().c_str(), baseUrl.empty() ? 0 : 1, voice ? 1 : 0,
      pipeline_->HandsFree() ? 1 : 0, context_.size());
  if (voice && !pipeline_->HandsFree() && !turnModelError_.empty())
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
    // A muted reply still plays (silently) and keeps its timing, so say why nothing is heard.
    else if (state == "speaking") text = ReplyVolume() > 0 ? "轴伊正在回复…" : "JPet 已静音，听不到轴伊的回复";
    else if (state == "replying") text = "轴伊正在回复…";
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
  const auto speechModel = SpeechModelStatus();
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

bool VoiceChat::SendText(const std::string& text, std::string& error) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) { error = "消息不能为空"; return false; }
  if (text.size() > 6000) { error = "消息太长，请精简后再发送"; return false; }
  auto* data = DataManager::GetInstance();
  if (!Voice::ValidWorkspace(data->GetConfig<std::string>("voice", "workspace_id", "")) ||
      !data->GetConfig<bool>("voice", "has_api_key", false)) {
    error = "请先在语音对话设置中填写 API Key 和业务空间 ID";
    return false;
  }
  if (!data->GetVoiceSettings().at("chat_base_url").get<std::string>().empty() &&
      !data->GetConfig<bool>("voice", "has_chat_api_key", false)) {
    error = "请在语音对话设置中填写自定义大模型的 API Key";
    return false;
  }
  std::lock_guard<std::mutex> lock(textMutex_);
  if (texts_.size() >= 4) { error = "消息发送太快了，请稍后再试"; return false; }
  texts_.push_back(text);
  return true;
}

bool VoiceChat::Connect(bool voice) {
  if (connected_ && (voice_ || !voice)) return true;
  auto* data = DataManager::GetInstance();
  const auto workspace = data->GetConfig<std::string>("voice", "workspace_id", "");
  if (!Voice::ValidWorkspace(workspace) || !data->GetConfig<bool>("voice", "has_api_key", false)) {
    SetState("error", "请先在对话 → 语音对话设置中填写 API Key 和业务空间 ID");
    return false;
  }
  std::string error;
  const auto key = Voice::LoadApiKey(LAppPal::WStringToString(LAppDefine::documentPath), error);
  if (!error.empty() || key.empty()) {
    SetState("error", error.empty() ? "未找到 API Key，请在语音对话设置中重新保存" : error);
    return false;
  }
  if (connected_) Close();  // Text-only so far; the conversation continues with voice.
  if (!StartPipeline(workspace, key, voice)) return false;
  connected_ = true;
  voice_ = voice;
  return true;
}

void VoiceChat::Begin() {
  auto* data = DataManager::GetInstance();
  if (!Connect(true)) {
    shortcut_.BlockUntilRelease();
    return;
  }
  if (!tools_) tools_ = std::make_unique<Voice::ToolExecutor>(Voice::MakeToolDependencies());
  capturedBytes_ = 0;
  capturedEnergy_ = 0;
  capturedPeak_ = 0;
  pipeline_->BeginInput(data->GetConfig<std::string>("voice", "input_mode", "toggle") == "hold");
  const auto settings = data->GetVoiceSettings();
  platform_->SetDevices(settings.at("input_device"), settings.at("output_device"));
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
  if (pipeline_) {
    pipeline_->Reset(failed);
    context_ = pipeline_->Context();
  }
  pipeline_.reset();  // Joins its recognition and synthesis workers; the synthesis model goes with it.
  {
    // SenseVoice holds about half a gigabyte; reload it (about a second) next time.
    std::lock_guard<std::mutex> lock(recognizerMutex_);
    localRecognizer_.reset();
    recognizerError_.clear();
  }
  connected_ = voice_ = microphoneEnabled_ = false;
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
    context_ = nlohmann::json::array();
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
  std::vector<std::string> texts;
  {
    std::lock_guard<std::mutex> lock(textMutex_);
    texts.swap(texts_);
  }
  for (const auto& text : texts) {
    if (!Connect(false)) break;
    LAppPal::PrintLog("[Voice] Text message bytes=%zu", text.size());
    if (!tools_) tools_ = std::make_unique<Voice::ToolExecutor>(Voice::MakeToolDependencies());
    pipeline_->SubmitText(text);
    lastActivity_ = Clock::now();
  }
  if (stopRequested_.exchange(false) && pipeline_) {
    pipeline_->StopReply();
    lastActivity_ = Clock::now();
  }
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
    Fail(voice_ ? "轴伊回复超时，请按快捷键重新开启麦克风" : "轴伊回复超时，请重新发送消息");
  else if (connected_ && !pipeline_->Busy() && !platform_->IsPlaying() && now - lastActivity_ > std::chrono::minutes(2)) {
    Close();
    SetState("idle");
  }
  busy_ = (pipeline_ && pipeline_->Busy()) || platform_->IsPlaying();
  {
    std::lock_guard<std::mutex> lock(statusMutex_);
    status_["replying"] = pipeline_ && pipeline_->Replying();
  }
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
