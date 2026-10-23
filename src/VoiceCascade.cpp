#include "VoiceCascade.hpp"

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <cstdio>
#include <cstring>
#include <random>

namespace Voice {
namespace {
constexpr size_t InputRate = TurnSampleRate;
constexpr uint64_t MaxStreamSamples = InputRate * 55;  // Manual commits stay under the 60 s limit.
constexpr size_t ContextMessages = 40;
constexpr int MaxRounds = 8;

std::string NewTaskId() {
  static std::mt19937_64 random{std::random_device{}()};
  const uint64_t a = random(), b = random();
  char text[40];
  std::snprintf(text, sizeof(text), "%08x-%04x-4%03x-%04x-%012llx", unsigned(a >> 32), unsigned((a >> 16) & 0xffff),
      unsigned(a & 0xfff), unsigned(0x8000 | ((b >> 48) & 0x3fff)), static_cast<unsigned long long>(b & 0xffffffffffffULL));
  return text;
}

std::string Trimmed(const std::string& text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

std::string Host(const std::string& workspace) { return "wss://" + workspace + ".cn-beijing.maas.aliyuncs.com"; }
}  // namespace

const nlohmann::json& CascadeCatalog() {
  static const nlohmann::json catalog = {
    {"chat_models", {
      {{"id", "qwen-plus"}, {"name", "通义千问 Plus（推荐）"}},
      {{"id", "qwen-flash"}, {"name", "通义千问 Flash · 更快"}},
      {{"id", "qwen-max"}, {"name", "通义千问 Max · 更强"}}}},
    // Companion and assistant voices from the CosyVoice voice list.
    {"tts_models", {
      {{"id", "cosyvoice-v3-flash"}, {"name", "CosyVoice v3 Flash（推荐）"}, {"voices", {
        {{"id", "longanhuan"}, {"name", "龙安欢 · 元气活泼"}},
        {{"id", "longanhuan_v3"}, {"name", "龙安欢 v3 · 元气活泼，支持方言"}},
        {{"id", "longhua_v3"}, {"name", "龙华 · 元气甜美"}},
        {{"id", "longanrou_v3"}, {"name", "龙安柔 · 温柔闺蜜"}},
        {{"id", "longanqin_v3"}, {"name", "龙安亲 · 亲和活泼"}},
        {{"id", "longanya_v3"}, {"name", "龙安雅 · 优雅"}},
        {{"id", "longyingling_v3"}, {"name", "龙应聆 · 温暖共情"}},
        {{"id", "longyingtao_v3"}, {"name", "龙应桃 · 温柔淡定"}},
        {{"id", "longyingmu_v3"}, {"name", "龙应沐 · 优雅知性"}},
        {{"id", "longanyang"}, {"name", "龙安洋 · 阳光男声"}},
        {{"id", "longanzhi_v3"}, {"name", "龙安智 · 睿智成熟男声"}},
        {{"id", "longyingxun_v3"}, {"name", "龙应询 · 青涩男声"}}}}},
      {{"id", "cosyvoice-v3-plus"}, {"name", "CosyVoice v3 Plus · 更自然"}, {"voices", {
        {{"id", "longanhuan"}, {"name", "龙安欢 · 元气活泼"}},
        {{"id", "longanyang"}, {"name", "龙安洋 · 阳光男声"}}}}}}}};
  return catalog;
}

bool ValidChatModel(const std::string& model) {
  return !model.empty() && model.size() <= 128 && std::all_of(model.begin(), model.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == ':' || c == '/';
  });
}

bool ValidTtsVoice(const std::string& model, const std::string& voice) {
  for (const auto& entry : CascadeCatalog()["tts_models"]) {
    if (entry["id"] != model) continue;
    const auto& voices = entry["voices"];
    return std::any_of(voices.begin(), voices.end(), [&](const auto& item) { return item["id"] == voice; });
  }
  return false;
}


// Transcribes utterances one at a time off the render thread.
class Pipeline::LocalWorker {
 public:
  struct Result { uint64_t id; std::string text; bool partial; };
  // Inline mode transcribes during Submit(), for deterministic tests.
  LocalWorker(Transcriber transcribe, bool threaded) : transcribe_(std::move(transcribe)) {
    if (threaded) worker_ = std::thread([this] { Run(); });
  }
  ~LocalWorker() {
    { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
  }
  void Submit(uint64_t id, std::string pcm, bool partial) {
    if (!worker_.joinable()) {
      auto text = transcribe_(pcm);
      std::lock_guard<std::mutex> lock(mutex_);
      results_.push_back({id, std::move(text), partial});
      return;
    }
    { std::lock_guard<std::mutex> lock(mutex_); jobs_.push_back({id, std::move(pcm), partial}); }
    wake_.notify_all();
  }
  void Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    jobs_.clear();
    results_.clear();
    ++generation_;
  }
  std::vector<Result> Poll() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto results = std::move(results_);
    results_.clear();
    return results;
  }

 private:
  struct Job { uint64_t id; std::string pcm; bool partial; };
  void Run() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
      wake_.wait(lock, [&] { return stopping_ || !jobs_.empty(); });
      if (stopping_) return;
      auto job = std::move(jobs_.front());
      jobs_.pop_front();
      const auto generation = generation_;
      lock.unlock();
      auto text = transcribe_(job.pcm);
      lock.lock();
      if (generation == generation_) results_.push_back({job.id, std::move(text), job.partial});
    }
  }
  Transcriber transcribe_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<Job> jobs_;
  std::vector<Result> results_;
  uint64_t generation_ = 0;
  bool stopping_ = false;
  std::thread worker_;
};

Pipeline::Pipeline(CascadeConfig config, Callbacks callbacks, nlohmann::json tools,
                   std::unique_ptr<TurnAnalyzer> analyzer, std::unique_ptr<ChatBackend> chat,
                   Transcriber transcribe, bool threadedRecognition)
    : config_(std::move(config)), callbacks_(std::move(callbacks)), tools_(std::move(tools)),
      analyzer_(std::move(analyzer)), chat_(std::move(chat)),
      local_(std::make_unique<LocalWorker>(std::move(transcribe), threadedRecognition)) {}

Pipeline::~Pipeline() = default;

void Pipeline::Status(const std::string& state, const std::string& message) {
  if (callbacks_.status) callbacks_.status(state, message);
}

void Pipeline::Log(const std::string& line) {
  if (callbacks_.log) callbacks_.log("[Voice] Cascade " + line);
}

void Pipeline::Record(ConversationEvent::Type type, const std::string& text, const std::string& id,
                      const nlohmann::json& data) {
  if (callbacks_.history) callbacks_.history({type, turn_, text, id, data});
}

std::string Pipeline::ReplyId() const { return "reply-" + std::to_string(turn_) + "-" + std::to_string(round_); }

bool Pipeline::BotBusy() const { return replyActive_ || (callbacks_.isPlaying && callbacks_.isPlaying()); }

bool Pipeline::WaitingForReply() const {
  const bool pendingInput = std::any_of(commits_.begin(), commits_.end(), [](const Commit& c) { return !c.tentative; });
  return listen_ != Listen::Active && (pendingInput || (replyActive_ &&
      (chatActive_ || !pendingTools_.empty() || task_ != Task::None || !queued_.empty())));
}

bool Pipeline::Busy() const { return inputOpen_ || streaming_ || !commits_.empty() || replyActive_; }

// ---- Input and turn detection ------------------------------------------------

void Pipeline::BeginInput(bool halfDuplex) {
  if (inputOpen_) return;
  inputOpen_ = true;
  hold_ = halfDuplex || !analyzer_;
  input_.clear();
  inputBase_ = 0;
  if (hold_) {
    // Push-to-talk: key-down interrupts and opens the turn immediately.
    if (BotBusy()) Interrupt();
    StartStream(0, false);
  } else {
    analyzer_->Reset();
  }
  Status("listening");
}

void Pipeline::AppendInput(const std::string& pcm) {
  if (!inputOpen_ || pcm.empty()) return;
  input_ += pcm;
  if (!hold_) analyzer_->Push(pcm);
  if (streaming_) FlushStream();
  // Keep two seconds for the pre-roll of the next turn, or everything not yet streamed.
  const uint64_t end = inputBase_ + input_.size() / 2;
  uint64_t keep = end > 2 * InputRate ? end - 2 * InputRate : 0;
  if (streaming_) keep = std::min(keep, streamFrom_);
  if (keep > inputBase_) {
    input_.erase(0, size_t(keep - inputBase_) * 2);
    inputBase_ = keep;
  }
}

void Pipeline::EndInput() {
  if (!inputOpen_) return;
  inputOpen_ = false;
  // Switching the microphone off ends the user's turn, even mid-sentence.
  if (streaming_) CommitStream();
  if (analyzer_) analyzer_->Reset();
  if (!BotBusy() && commits_.empty()) Status("idle");
}

void Pipeline::StartStream(uint64_t from, bool tentative) {
  streaming_ = true;
  streamFrom_ = streamStart_ = std::max(from, inputBase_);
  listen_ = tentative ? Listen::Tentative : Listen::Active;
  interim_.clear();
  localAudio_.clear();
  partialAt_ = 0;
  FlushStream();
}

void Pipeline::FlushStream() {
  const uint64_t end = inputBase_ + input_.size() / 2;
  if (streamFrom_ >= end) return;
  auto audio = input_.substr(size_t(streamFrom_ - inputBase_) * 2);
  streamFrom_ = end;
  localAudio_ += audio;
  // Speech over a reply needs words before it may interrupt: transcribe what
  // has been said so far every half second.
  if (listen_ == Listen::Tentative && !partialPending_ && localAudio_.size() - partialAt_ >= InputRate) {
    partialAt_ = localAudio_.size();
    partialPending_ = true;
    local_->Submit(0, localAudio_, true);
  }
}

void Pipeline::CommitStream() {
  FlushStream();
  const bool tentative = listen_ == Listen::Tentative;
  streaming_ = false;
  listen_ = Listen::Idle;
  // Less than 100 ms is a click or breath; an empty commit would be an error.
  if (streamFrom_ - streamStart_ < InputRate / 10) {
    if (!tentative && !BotBusy()) Status(inputOpen_ ? "listening" : "idle");
    return;
  }
  const auto id = ++localJobs_;
  commits_.push_back({tentative, id});
  local_->Submit(id, std::move(localAudio_), false);
  localAudio_.clear();
  if (!tentative) Status("thinking");
}

// ---- Recognition (local SenseVoice) ----------------------------------------------

void Pipeline::OnInterim(const std::string& text) {
  interim_ = text;
  // A tentative turn becomes a barge-in once it is clearly not our own echo.
  if (listen_ == Listen::Tentative && CodePoints(Trimmed(interim_)) >= 2 && !LooksLikeEcho(interim_, replyText_)) {
    Log("barge-in confirmed by transcript");
    Interrupt();
    listen_ = Listen::Active;
    Status("listening");
  }
}

void Pipeline::OnTranscript(const std::string& text, bool tentative) {
  if (text.empty() || (tentative && LooksLikeEcho(text, replyText_)) ||
      (tentative && CodePoints(text) < 2)) {
    Log(tentative ? "speech during reply ignored as echo" : "empty transcript");
    if (!tentative && !BotBusy()) Status(inputOpen_ ? "listening" : "idle");
    return;
  }
  if (BotBusy()) Interrupt();
  StartReply(text);
}

// ---- Chat --------------------------------------------------------------------

void Pipeline::StartReply(const std::string& text) {
  ++turn_;
  round_ = rounds_ = 0;
  replyActive_ = true;
  audible_ = false;
  replyText_.clear();
  segments_.clear();
  toolCount_ = 0;
  // Align the played-sample counters with this reply's segments.
  if (!callbacks_.isPlaying || !callbacks_.isPlaying()) {
    callbacks_.stopPlayback();
    enqueued_ = 0;
  }
  Record(ConversationEvent::Type::Started);
  Record(ConversationEvent::Type::UserTranscript, text);
  messages_.push_back({{"role", "user"}, {"content", text}});
  TrimContext();
  EnsureSynthesis();
  if (callbacks_.transcript) callbacks_.transcript("");
  RequestChat();
}

void Pipeline::RequestChat() {
  if (++rounds_ > MaxRounds) {
    FinishReply(ConversationEvent::Type::Failed);
    Status(inputOpen_ ? "listening" : "idle", "工具调用次数过多，本次回复已停止");
    return;
  }
  ++round_;
  roundText_.clear();
  splitter_.Reset();
  roundDone_ = false;
  chatActive_ = true;
  auto messages = nlohmann::json::array({{{"role", "system"}, {"content", Instructions() +
      "你的回复会被转成语音朗读：不要使用 Markdown、列表符号、表情或链接，一般不超过三句话。"}}});
  for (const auto& message : messages_) messages.push_back(message);
  nlohmann::json body = {{"model", config_.chatModel}, {"stream", true}, {"messages", messages}, {"max_tokens", 1024}};
  if (!tools_.empty()) body["tools"] = tools_;
  if (config_.bailianChat) body["enable_thinking"] = false;
  chat_->Start(config_.chatUrl, config_.chatKey, body);
  Status("thinking");
}

void Pipeline::OnChat(const ChatEvent& event) {
  if (!chatActive_) return;
  if (event.type == ChatEvent::Type::Text) {
    roundText_ += event.text;
    replyText_ += event.text;
    if (callbacks_.transcript) callbacks_.transcript(replyText_);
    Record(ConversationEvent::Type::AssistantTranscript, roundText_, ReplyId());
    for (const auto& sentence : splitter_.Push(event.text)) Speak(sentence);
    return;
  }
  chatActive_ = false;
  if (event.type == ChatEvent::Type::Failed) {
    Log("chat failed");
    FinishReply(ConversationEvent::Type::Failed);
    Status(inputOpen_ ? "listening" : "idle", event.text);
    return;
  }
  const auto rest = splitter_.Flush();
  if (!rest.empty()) Speak(rest);
  if (event.tools.empty()) { roundDone_ = true; return; }
  auto calls = nlohmann::json::array();
  std::vector<ToolCall> accepted;
  for (const auto& call : event.tools) {
    if (++toolCount_ > 16) break;
    calls.push_back({{"id", call.id}, {"type", "function"}, {"function", {{"name", call.name}, {"arguments", call.arguments}}}});
    accepted.push_back(call);
  }
  if (accepted.empty()) {
    FinishReply(ConversationEvent::Type::Failed);
    Status(inputOpen_ ? "listening" : "idle", "工具调用次数过多，本次回复已停止");
    return;
  }
  // Every tool call in the context gets a result, even when interrupted.
  messages_.push_back({{"role", "assistant"}, {"content", roundText_.empty() ? nlohmann::json(nullptr) : nlohmann::json(roundText_)},
                       {"tool_calls", calls}});
  for (const auto& call : accepted) {
    pendingTools_.insert(call.id);
    Record(ConversationEvent::Type::ToolStarted, call.name, call.id,
        call.arguments.size() <= 16384 ? nlohmann::json::parse(call.arguments, nullptr, false) : nlohmann::json(nullptr));
  }
  Status("tool", "正在执行工具…");
  if (callbacks_.tools) callbacks_.tools(accepted);
  else for (const auto& call : accepted) CompleteTool(call.id, {{"ok", false}, {"error", "工具不可用"}});
}

void Pipeline::CompleteTool(const std::string& callId, const nlohmann::json& result) {
  if (!replyActive_ || !pendingTools_.erase(callId)) return;
  auto output = result.is_object() && result.contains("ok") && result["ok"].is_boolean() ? result.dump() :
      R"({"ok":false,"error":"工具没有返回有效的执行结果。"})";
  if (output.size() > 16 * 1024) output = R"({"ok":false,"error":"工具结果过大，请缩小查询范围。"})";
  Record(ConversationEvent::Type::ToolCompleted, "", callId, nlohmann::json::parse(output));
  messages_.push_back({{"role", "tool"}, {"tool_call_id", callId}, {"content", output}});
  if (pendingTools_.empty()) RequestChat();
}

void Pipeline::TrimContext() {
  // Drop whole exchanges from the front so tool results never lose their call.
  while (messages_.size() > ContextMessages) {
    messages_.erase(messages_.begin());
    while (!messages_.empty() && messages_.front().value("role", std::string{}) != "user") messages_.erase(messages_.begin());
  }
}

// ---- Synthesis (CosyVoice, one task per sentence) ---------------------------

void Pipeline::Speak(const std::string& sentence) {
  segments_.push_back({sentence, round_});
  queued_.push_back(segments_.size() - 1);
  StartSynthesis();
}

void Pipeline::EnsureSynthesis() {
  if (synthesis_ != Link::Closed) return;
  // Sends wait for the handshake in the platform layer; there is no session event.
  synthesis_ = Link::Ready;
  callbacks_.connect(SynthesisChannel, Host(config_.workspace) + "/api-ws/v1/inference", config_.apiKey);
}

void Pipeline::StartSynthesis() {
  if (task_ != Task::None || queued_.empty()) return;
  EnsureSynthesis();
  taskSegment_ = queued_.front();
  queued_.pop_front();
  taskId_ = NewTaskId();
  task_ = Task::Starting;
  carry_.clear();
  callbacks_.send(SynthesisChannel, {
    {"header", {{"action", "run-task"}, {"task_id", taskId_}, {"streaming", "duplex"}}},
    {"payload", {{"task_group", "audio"}, {"task", "tts"}, {"function", "SpeechSynthesizer"}, {"model", config_.ttsModel},
      {"parameters", {{"text_type", "PlainText"}, {"voice", config_.ttsVoice}, {"format", "pcm"},
                      {"sample_rate", ReplySampleRate}, {"volume", 50}, {"rate", 1}, {"pitch", 1}}},
      {"input", nlohmann::json::object()}}}});
}

void Pipeline::OnSynthesis(const nlohmann::json& event) {
  if (!event.contains("header") || !event["header"].is_object()) return;
  const auto& header = event["header"];
  if (header.value("task_id", std::string{}) != taskId_ || task_ == Task::None) return;
  const auto name = header.value("event", std::string{});
  if (name == "task-started" && task_ == Task::Starting) {
    task_ = Task::Running;
    const auto continueTask = [&](const char* action, nlohmann::json input) {
      callbacks_.send(SynthesisChannel, {{"header", {{"action", action}, {"task_id", taskId_}, {"streaming", "duplex"}}},
                                         {"payload", {{"input", std::move(input)}}}});
    };
    continueTask("continue-task", {{"text", segments_[taskSegment_].text}});
    continueTask("finish-task", nlohmann::json::object());
  } else if (name == "task-finished" || name == "task-failed") {
    if (name == "task-failed") Log("synthesis failed code=" + header.value("error_code", std::string{}));
    task_ = Task::None;
    StartSynthesis();
  }
}

void Pipeline::OnAudio(const std::string& bytes) {
  if (task_ == Task::None) return;
  auto pcm = carry_ + bytes;
  carry_.clear();
  if (pcm.size() % 2) { carry_ = pcm.substr(pcm.size() - 1); pcm.pop_back(); }
  if (pcm.empty()) return;
  auto& segment = segments_[taskSegment_];
  if (!segment.started) { segment.started = true; segment.start = enqueued_; }
  segment.samples += pcm.size() / 2;
  enqueued_ += pcm.size() / 2;
  callbacks_.play(pcm);
  if (!audible_) { audible_ = true; Status("speaking"); }
}

std::string Pipeline::SpokenText(uint64_t played) const {
  std::string spoken;
  for (const auto& segment : segments_) {
    if (segment.round != round_ || !segment.started || segment.start >= played) continue;
    if (segment.start + segment.samples <= played) { spoken += segment.text; continue; }
    spoken += PrefixFraction(segment.text, double(played - segment.start) / double(segment.samples));
    break;
  }
  return spoken;
}

// ---- Interruption and completion ----------------------------------------------

void Pipeline::Interrupt() {
  const uint64_t played = callbacks_.playedSamples ? callbacks_.playedSamples() : 0;
  callbacks_.stopPlayback();
  enqueued_ = 0;
  if (!replyActive_) return;
  Log("interrupted");
  chat_->Cancel();
  chatActive_ = false;
  queued_.clear();
  if (task_ != Task::None) {
    // CosyVoice has no cancel and its audio frames carry no task id: reconnect.
    callbacks_.disconnect(SynthesisChannel);
    synthesis_ = Link::Closed;
    task_ = Task::None;
    EnsureSynthesis();
  }
  if (!pendingTools_.empty()) {
    if (callbacks_.interruptTools) callbacks_.interruptTools();
    for (const auto& id : pendingTools_) {
      const nlohmann::json result = {{"ok", false}, {"interrupted", true},
          {"error", "用户已打断；未开始的操作已取消，正在执行的操作可能已经生效，请查询实际状态。"}};
      Record(ConversationEvent::Type::ToolCompleted, "", id, result);
      messages_.push_back({{"role", "tool"}, {"tool_call_id", id}, {"content", result.dump()}});
    }
    pendingTools_.clear();
  } else {
    // Only what the user heard of this round stays in the conversation.
    const auto spoken = SpokenText(played);
    if (!spoken.empty()) messages_.push_back({{"role", "assistant"}, {"content", spoken}});
    Record(ConversationEvent::Type::AssistantTranscript, spoken, ReplyId());
  }
  replyActive_ = false;
  Record(ConversationEvent::Type::Interrupted);
}

void Pipeline::FinishReply(ConversationEvent::Type type) {
  if (!replyActive_) return;
  if (!roundText_.empty() && pendingTools_.empty()) messages_.push_back({{"role", "assistant"}, {"content", roundText_}});
  chat_->Cancel();
  chatActive_ = false;
  queued_.clear();
  replyActive_ = false;
  Record(type);
}

void Pipeline::Reset(bool failed) {
  if (replyActive_) {
    chat_->Cancel();
    replyActive_ = chatActive_ = false;
    Record(failed ? ConversationEvent::Type::Failed : ConversationEvent::Type::Interrupted);
  }
  callbacks_.stopPlayback();
  enqueued_ = 0;
  inputOpen_ = streaming_ = false;
  listen_ = Listen::Idle;
  input_.clear();
  inputBase_ = 0;
  commits_.clear();
  queued_.clear();
  pendingTools_.clear();
  task_ = Task::None;
  synthesis_ = Link::Closed;
  if (analyzer_) analyzer_->Reset();
  local_->Reset();
  localAudio_.clear();
  partialPending_ = false;
}

// ---- Event plumbing ------------------------------------------------------------

void Pipeline::Receive(const Event& event) {
  if (event.channel == SynthesisChannel && event.type == Event::Type::Binary) { OnAudio(event.data); return; }
  if (event.channel != SynthesisChannel) return;
  if (event.type == Event::Type::Error) {
    Log("synthesis connection lost");
    synthesis_ = Link::Closed;
    if (task_ != Task::None) {
      task_ = Task::None;
      StartSynthesis();  // Reconnects and continues with the next sentence.
    }
    return;
  }
  if (event.type != Event::Type::Message) return;
  const auto message = nlohmann::json::parse(event.data, nullptr, false);
  if (message.is_object()) OnSynthesis(message);
}

bool Pipeline::Tick() {
  bool progressed = false;
  if (analyzer_) for (const auto& result : analyzer_->Poll()) {
    progressed = true;
    if (!inputOpen_ || hold_) continue;
    if (result.event == TurnDetector::Event::SpeechStarted) {
      if (!streaming_) StartStream(result.turnStart, BotBusy());
      if (listen_ == Listen::Active) Status("listening");
    } else if (result.event == TurnDetector::Event::TurnComplete) {
      Log("turn complete smart_turn=" + std::to_string(result.probability));
      if (streaming_) CommitStream();
    }
  }
  if (streaming_ && streamFrom_ - streamStart_ >= MaxStreamSamples) CommitStream();
  for (auto& result : local_->Poll()) {
    progressed = true;
    if (result.partial) {
      partialPending_ = false;
      if (streaming_) OnInterim(Trimmed(result.text));
      continue;
    }
    const auto found = std::find_if(commits_.begin(), commits_.end(), [&](const Commit& c) { return c.id == result.id; });
    if (found == commits_.end()) continue;
    const bool tentative = found->tentative;
    commits_.erase(found);
    OnTranscript(Trimmed(result.text), tentative);
  }
  for (const auto& event : chat_->Poll()) { progressed = true; OnChat(event); }
  StartSynthesis();
  if (replyActive_ && !chatActive_ && pendingTools_.empty() && roundDone_ && queued_.empty() &&
      task_ == Task::None && !(callbacks_.isPlaying && callbacks_.isPlaying())) {
    FinishReply(ConversationEvent::Type::Completed);
    if (listen_ != Listen::Active) Status(inputOpen_ ? "listening" : "idle");
    progressed = true;
  }
  return progressed;
}
}  // namespace Voice
