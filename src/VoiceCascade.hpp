#pragma once

#include "VoiceLlm.hpp"
#include "VoiceCommon.hpp"
#include "VoiceSynthesis.hpp"
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

struct CascadeConfig {
  std::string chatUrl, chatKey, chatModel;
  nlohmann::json chatOptions = nlohmann::json::object();  // Provider-specific request fields.
};

// Voice conversation engine (speech recognition + chat model + speech
// synthesis), structured after Pipecat: local turn detection decides when the
// user has finished, local SenseVoice transcribes each turn, the reply streams
// sentence by sentence into local speech synthesis in Joi's voice, and an
// interruption keeps only what was actually played in the conversation context.
class Pipeline {
 public:
  struct Callbacks {
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
  // on a worker thread unless threadedRecognition is false (tests). A null
  // synthesizer gives a text-only pipeline that only takes SubmitText().
  Pipeline(CascadeConfig config, Callbacks callbacks, nlohmann::json tools,
           std::unique_ptr<TurnAnalyzer> analyzer, std::unique_ptr<ChatBackend> chat,
           std::unique_ptr<Synthesizer> synthesizer, Transcriber transcribe, bool threadedRecognition = true);
  ~Pipeline();

  void Reset(bool failed = false);
  void BeginInput(bool halfDuplex = false);
  void AppendInput(const std::string& pcm);
  void EndInput();
  void CompleteTool(const std::string& callId, const nlohmann::json& result);
  // A typed message from the panel: interrupts any reply and is answered in
  // text only, never spoken.
  void SubmitText(const std::string& text);
  // Stops the current reply, keeping what was shown or heard in the context.
  void StopReply();
  // Chat context, carried across pipelines so voice and text share one conversation.
  const nlohmann::json& Context() const { return messages_; }
  void SetContext(nlohmann::json messages) { messages_ = std::move(messages); TrimContext(); }
  bool Recording() const { return inputOpen_; }
  bool Replying() const { return replyActive_; }
  bool WaitingForReply() const;
  bool Busy() const;
  bool HandsFree() const { return analyzer_ != nullptr; }

  // Drives turn detection, chat streaming and synthesis; returns true when
  // something progressed. Call every frame.
  bool Tick();

 private:
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

  void StartReply(const std::string& text, bool voice = true);
  void RequestChat();
  void OnChat(const ChatEvent& event);
  void Speak(const std::string& sentence);
  void StartSynthesis();
  void OnAudio(const std::string& pcm);
  std::string SpokenText(uint64_t played) const;
  void Interrupt();
  void FinishReply(ConversationEvent::Type type);
  void TrimContext();

  CascadeConfig config_;
  Callbacks callbacks_;
  nlohmann::json tools_;
  std::unique_ptr<TurnAnalyzer> analyzer_;
  std::unique_ptr<ChatBackend> chat_;
  std::unique_ptr<Synthesizer> synthesizer_;

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
  bool voiceReply_ = true;           // Spoken; typed messages get text-only replies.
  std::string roundText_, replyText_;
  SentenceSplitter splitter_;
  std::set<std::string> pendingTools_;
  size_t toolCount_ = 0;
  // Base64 JPEGs returned by tools in this reply. Sent with each of its chat
  // requests but kept out of the context: a screenshot is large and only
  // matters to the reply that asked for it.
  std::vector<std::string> images_;

  // Synthesis: one sentence at a time, so each segment's audio interval is known.
  std::vector<Segment> segments_;
  std::deque<size_t> queued_;
  bool synthesizing_ = false;
  uint64_t taskId_ = 0;
  size_t taskSegment_ = 0;
  uint64_t enqueued_ = 0;   // Samples handed to play() since the last stopPlayback.
};
}  // namespace Voice
