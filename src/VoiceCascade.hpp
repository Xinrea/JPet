#pragma once

#include "VoiceLlm.hpp"
#include "VoicePlatform.hpp"
#include "VoiceCommon.hpp"
#include "VoiceText.hpp"
#include "VoiceTurn.hpp"

#include <deque>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace Voice {
constexpr int ReplySampleRate = 24000;

constexpr const char* DefaultChatModel = "qwen-plus";
constexpr const char* DefaultTtsModel = "cosyvoice-v3-flash";
constexpr const char* DefaultTtsVoice = "longanhuan";
// Bailian chat models offered in settings and the CosyVoice models with their
// voices: {chat_models: [{id, name}], tts_models: [{id, name, voices: [{id, name}]}]}.
const nlohmann::json& CascadeCatalog();
bool ValidChatModel(const std::string& model);  // Any provider's model name.
bool ValidTtsVoice(const std::string& model, const std::string& voice);

struct CascadeConfig {
  std::string workspace, apiKey;        // Bailian: synthesis and the default chat endpoint.
  std::string chatUrl, chatKey, chatModel;
  bool bailianChat = true;              // Adds Bailian-only request fields.
  std::string ttsModel, ttsVoice;
};

// Voice conversation engine (speech recognition + chat model + speech
// synthesis), structured after Pipecat: local turn detection decides when the
// user has finished, local SenseVoice transcribes each turn, the reply streams
// sentence by sentence into per-sentence CosyVoice tasks, and an interruption
// keeps only what was actually played in the conversation context.
class Pipeline {
 public:
  struct Callbacks {
    std::function<void(int channel, const std::string& url, const std::string& apiKey)> connect;
    std::function<void(int channel)> disconnect;
    std::function<void(int channel, const nlohmann::json& message)> send;
    std::function<void(const std::string& pcm)> play;
    std::function<bool()> isPlaying;
    std::function<uint64_t()> playedSamples;
    std::function<void()> stopPlayback;
    std::function<void(const std::string& state, const std::string& message)> status;
    std::function<void(const std::string& reply)> transcript;
    std::function<void(const std::vector<ToolCall>& calls)> tools;
    std::function<void(const ConversationEvent& event)> history;
    std::function<void()> interruptTools;
    std::function<void(const std::string& line)> log;
  };
  // Transcribes one utterance of 16 kHz mono PCM on this machine.
  using Transcriber = std::function<std::string(const std::string& pcm)>;
  // A null analyzer limits the pipeline to push-to-talk turns. Recognition runs
  // on a worker thread unless threadedRecognition is false (tests).
  Pipeline(CascadeConfig config, Callbacks callbacks, nlohmann::json tools,
           std::unique_ptr<TurnAnalyzer> analyzer, std::unique_ptr<ChatBackend> chat,
           Transcriber transcribe, bool threadedRecognition = true);
  ~Pipeline();

  void Reset(bool failed = false);
  void BeginInput(bool halfDuplex = false);
  void AppendInput(const std::string& pcm);
  void EndInput();
  void CompleteTool(const std::string& callId, const nlohmann::json& result);
  bool Recording() const { return inputOpen_; }
  bool WaitingForReply() const;
  bool Busy() const;
  bool HandsFree() const { return analyzer_ != nullptr; }

  // Synthesis channel events (messages, audio and errors).
  void Receive(const Event& event);
  // Drives turn detection, chat streaming and synthesis; returns true when
  // something progressed. Call every frame.
  bool Tick();

 private:
  enum class Link { Closed, Connecting, Ready };
  enum class Listen { Idle, Tentative, Active };
  struct Commit { bool tentative; uint64_t id; };
  struct Segment { std::string text; int round; uint64_t start = 0, samples = 0; bool started = false; };

  void Status(const std::string& state, const std::string& message = "");
  void Log(const std::string& line);
  void Record(ConversationEvent::Type type, const std::string& text = "", const std::string& id = "",
              const nlohmann::json& data = nlohmann::json::object());
  bool BotBusy() const;
  std::string ReplyId() const;

  void StartStream(uint64_t from, bool tentative);
  void FlushStream();
  void CommitStream();
  void OnInterim(const std::string& text);
  void OnTranscript(const std::string& text, bool tentative);

  void StartReply(const std::string& text);
  void RequestChat();
  void OnChat(const ChatEvent& event);
  void Speak(const std::string& sentence);
  void EnsureSynthesis();
  void StartSynthesis();
  void OnSynthesis(const nlohmann::json& event);
  void OnAudio(const std::string& bytes);
  std::string SpokenText(uint64_t played) const;
  void Interrupt();
  void FinishReply(ConversationEvent::Type type);
  void TrimContext();

  CascadeConfig config_;
  Callbacks callbacks_;
  nlohmann::json tools_;
  std::unique_ptr<TurnAnalyzer> analyzer_;
  std::unique_ptr<ChatBackend> chat_;

  // Microphone timeline shared with the analyzer: input_ holds samples from
  // absolute index inputBase_.
  bool inputOpen_ = false, hold_ = false;
  std::string input_;
  uint64_t inputBase_ = 0;

  // The user's current speech and its recognition.
  Listen listen_ = Listen::Idle;
  bool streaming_ = false;
  uint64_t streamFrom_ = 0, streamStart_ = 0;
  std::string interim_;
  std::deque<Commit> commits_;
  class LocalWorker;
  std::unique_ptr<LocalWorker> local_;
  std::string localAudio_;           // The current utterance for local recognition.
  size_t partialAt_ = 0;             // localAudio_ size at the last partial transcript.
  bool partialPending_ = false;
  uint64_t localJobs_ = 0;

  // The reply to the current user turn, possibly over several tool rounds.
  nlohmann::json messages_ = nlohmann::json::array();
  uint64_t turn_ = 0;
  int round_ = 0, rounds_ = 0;
  bool replyActive_ = false, chatActive_ = false, roundDone_ = false, audible_ = false;
  std::string roundText_, replyText_;
  SentenceSplitter splitter_;
  std::set<std::string> pendingTools_;
  size_t toolCount_ = 0;

  // Synthesis: one CosyVoice task per sentence on one connection.
  Link synthesis_ = Link::Closed;
  std::vector<Segment> segments_;
  std::deque<size_t> queued_;
  enum class Task { None, Starting, Running } task_ = Task::None;
  std::string taskId_;
  size_t taskSegment_ = 0;
  uint64_t enqueued_ = 0;   // Samples handed to play() since the last stopPlayback.
  std::string carry_;       // Odd byte between binary frames.
};
}  // namespace Voice
