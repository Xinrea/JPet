#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Voice {
class OnnxModel;

constexpr int TurnSampleRate = 16000;

// Silero VAD v5 at 16 kHz: 512-sample frames, 64 samples of context carried
// between frames and a recurrent state, as in Pipecat's SileroOnnxModel.
class SileroVad {
 public:
  static constexpr size_t Frame = 512;
  explicit SileroVad(std::unique_ptr<OnnxModel> model);
  ~SileroVad();
  // Speech probability for one frame of samples in [-1, 1]; -1 on failure.
  float Probability(const float* frame);
  void Reset();

 private:
  std::unique_ptr<OnnxModel> model_;
  std::vector<float> state_, context_;
};

// Whisper log-mel features for Smart Turn v3: 8 s of 16 kHz audio (shorter input
// is zero-padded at the end) to an 80 x 800 row-major matrix, matching
// transformers.WhisperFeatureExtractor(chunk_length=8, do_normalize=True).
std::vector<float> WhisperFeatures(const std::vector<float>& audio);

// Smart Turn v3: probability that the speaker finished their turn, from the
// last 8 s of the turn (zero-padded at the front when shorter).
class SmartTurn {
 public:
  explicit SmartTurn(std::unique_ptr<OnnxModel> model);
  ~SmartTurn();
  float Probability(const std::vector<int16_t>& audio);

 private:
  std::unique_ptr<OnnxModel> model_;
};

// Pipecat-style turn detection: the VAD state machine (confidence, start and
// stop hysteresis, volume gate) decides when speech starts and pauses; at each
// pause Smart Turn decides whether the turn is over, and a long silence ends it
// regardless.
class TurnDetector {
 public:
  struct Options {
    float confidence = 0.7f;
    double startSecs = 0.2, stopSecs = 0.2;
    double minLevelDbfs = -50;      // Frames quieter than this never count as speech.
    double preSpeechSecs = 0.5;     // Audio kept before the confirmed start.
    double silenceSecs = 3.0;       // Ends an unfinished turn after this pause.
    double windowSecs = 8.0;        // Smart Turn input length.
    float completeProbability = 0.5f;
  };
  enum class Event { SpeechStarted, SpeechPaused, SpeechResumed, TurnComplete };
  using Vad = std::function<float(const float* frame)>;
  using Endpoint = std::function<float(const std::vector<int16_t>& audio)>;

  TurnDetector(Vad vad, Endpoint endpoint, Options options);
  TurnDetector(Vad vad, Endpoint endpoint) : TurnDetector(std::move(vad), std::move(endpoint), Options()) {}
  // Feeds microphone PCM and returns the events it produced, in order.
  std::vector<Event> Process(const int16_t* samples, size_t count);
  // Starts over, e.g. when the microphone is switched off.
  void Reset();
  bool InTurn() const { return inTurn_; }
  bool Speaking() const { return state_ == State::Speaking || state_ == State::Stopping; }
  // Audio of the current turn from just before speech started (pre-roll included).
  const std::vector<int16_t>& TurnAudio() const { return turn_; }
  // Absolute index (samples fed since construction or Reset) where the current
  // or last turn's audio, pre-roll included, begins.
  uint64_t TurnStart() const { return turnStart_; }
  // Last Smart Turn probability, for diagnostics.
  float LastProbability() const { return probability_; }

 private:
  enum class State { Quiet, Starting, Speaking, Stopping };
  void Frame(const int16_t* frame, std::vector<Event>& events);
  Vad vad_;
  Endpoint endpoint_;
  Options options_;
  State state_ = State::Quiet;
  int startFrames_, stopFrames_, count_ = 0;
  std::vector<int16_t> pending_;   // Samples waiting for a full VAD frame.
  std::deque<int16_t> recent_;     // Pre-roll ring buffer.
  std::vector<int16_t> turn_;
  bool inTurn_ = false, paused_ = false;
  size_t silentSamples_ = 0;
  float probability_ = 0;
  uint64_t processed_ = 0, turnStart_ = 0;
};

// Runs a TurnDetector on its own thread so Smart Turn inference (tens of
// milliseconds) never stalls the caller. Inline mode runs it synchronously.
class TurnAnalyzer {
 public:
  struct Result {
    TurnDetector::Event event;
    uint64_t turnStart;   // TurnDetector::TurnStart() when the event fired.
    float probability;    // Smart Turn probability for pauses and completions.
  };
  TurnAnalyzer(std::unique_ptr<TurnDetector> detector, bool threaded = true);
  ~TurnAnalyzer();
  void Push(const std::string& pcm);
  // Starts a new sample timeline; queued audio before the reset is dropped.
  void Reset();
  std::vector<Result> Poll();

 private:
  std::vector<Result> Analyze(const std::string& pcm);
  std::unique_ptr<TurnDetector> detector_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::string> queue_;  // Empty string marks a reset.
  std::vector<Result> results_;
  bool stopping_ = false;
  uint64_t resets_ = 0;
  std::thread worker_;
};
}  // namespace Voice
