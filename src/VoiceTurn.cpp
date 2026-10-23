#include "VoiceTurn.hpp"
#include "VoiceOnnx.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>

namespace Voice {
namespace {
constexpr double Pi = 3.14159265358979323846;
constexpr int FftSize = 400, Hop = 160, Bins = FftSize / 2 + 1, Mels = 80, Frames = 800;
constexpr size_t WindowSamples = 8 * TurnSampleRate;

// Twiddle factors exp(-2 pi i j / 400); a size-n sub-transform uses every (400/n)th.
const std::vector<std::complex<double>>& Twiddles() {
  static const std::vector<std::complex<double>> table = [] {
    std::vector<std::complex<double>> values(FftSize);
    for (int j = 0; j < FftSize; ++j) values[j] = std::polar(1.0, -2 * Pi * j / FftSize);
    return values;
  }();
  return table;
}

// Mixed-radix decimation in time; Whisper's 400-point frames factor as 2^4 * 5^2.
void Dft(const double* input, size_t stride, size_t n, std::complex<double>* output, std::complex<double>* scratch) {
  if (n == 1) { output[0] = input[0]; return; }
  size_t radix = 2;
  while (n % radix) ++radix;
  const size_t m = n / radix, step = FftSize / n;
  const auto& twiddles = Twiddles();
  for (size_t r = 0; r < radix; ++r) Dft(input + r * stride, stride * radix, m, scratch + r * m, output + r * m);
  for (size_t k = 0; k < n; ++k) {
    std::complex<double> sum = 0;
    for (size_t r = 0; r < radix; ++r) sum += scratch[r * m + k % m] * twiddles[(r * k % n) * step];
    output[k] = sum;
  }
}

double HertzToMel(double hertz) {
  return hertz >= 1000 ? 15 + std::log(hertz / 1000) * 27 / std::log(6.4) : 3 * hertz / 200;
}

double MelToHertz(double mel) {
  return mel >= 15 ? 1000 * std::exp(std::log(6.4) / 27 * (mel - 15)) : 200 * mel / 3;
}

// Slaney-normalized triangular filters, [bin][mel], as in transformers.audio_utils.
const std::vector<double>& MelFilters() {
  static const std::vector<double> filters = [] {
    std::vector<double> edges(Mels + 2), result(Bins * Mels);
    const double top = HertzToMel(TurnSampleRate / 2.0);
    for (int i = 0; i < Mels + 2; ++i) edges[i] = MelToHertz(top * i / (Mels + 1));
    for (int bin = 0; bin < Bins; ++bin) {
      const double frequency = double(TurnSampleRate / 2) * bin / (Bins - 1);
      for (int mel = 0; mel < Mels; ++mel) {
        const double down = (frequency - edges[mel]) / (edges[mel + 1] - edges[mel]);
        const double up = (edges[mel + 2] - frequency) / (edges[mel + 2] - edges[mel + 1]);
        result[bin * Mels + mel] = std::max(0.0, std::min(down, up)) * 2 / (edges[mel + 2] - edges[mel]);
      }
    }
    return result;
  }();
  return filters;
}
}  // namespace

std::vector<float> WhisperFeatures(const std::vector<float>& audio) {
  std::vector<double> x(WindowSamples, 0.0);
  std::copy_n(audio.begin(), std::min(audio.size(), WindowSamples), x.begin());
  double mean = 0, variance = 0;
  for (const double value : x) mean += value;
  mean /= double(x.size());
  for (const double value : x) variance += (value - mean) * (value - mean);
  variance /= double(x.size());
  const double scale = 1 / std::sqrt(variance + 1e-7);
  for (auto& value : x) value = static_cast<float>((value - mean) * scale);

  // Centered frames with reflect padding (numpy "reflect" excludes the edge).
  const int pad = FftSize / 2;
  std::vector<double> padded(x.size() + 2 * pad);
  for (size_t i = 0; i < padded.size(); ++i) {
    long index = long(i) - pad;
    if (index < 0) index = -index;
    if (index >= long(x.size())) index = 2 * (long(x.size()) - 1) - index;
    padded[i] = x[size_t(index)];
  }
  std::vector<double> window(FftSize), frame(FftSize), power(Bins);
  for (int i = 0; i < FftSize; ++i) window[i] = 0.5 - 0.5 * std::cos(2 * Pi * i / FftSize);
  std::vector<std::complex<double>> spectrum(FftSize), scratch(FftSize);
  const auto& filters = MelFilters();
  std::vector<double> mel(size_t(Mels) * Frames);
  double peak = -1e30;
  // The reference drops the trailing frame, so only the first 800 are computed.
  for (int f = 0; f < Frames; ++f) {
    for (int i = 0; i < FftSize; ++i) frame[i] = padded[size_t(f) * Hop + i] * window[i];
    Dft(frame.data(), 1, FftSize, spectrum.data(), scratch.data());
    for (int bin = 0; bin < Bins; ++bin) power[bin] = std::norm(spectrum[bin]);
    for (int m = 0; m < Mels; ++m) {
      double sum = 0;
      for (int bin = 0; bin < Bins; ++bin) sum += filters[bin * Mels + m] * power[bin];
      const double value = std::log10(std::max(1e-10, sum));
      mel[size_t(m) * Frames + f] = value;
      peak = std::max(peak, value);
    }
  }
  std::vector<float> features(mel.size());
  for (size_t i = 0; i < mel.size(); ++i) features[i] = float((std::max(mel[i], peak - 8.0) + 4.0) / 4.0);
  return features;
}

SileroVad::SileroVad(std::unique_ptr<OnnxModel> model) : model_(std::move(model)) { Reset(); }
SileroVad::~SileroVad() = default;

void SileroVad::Reset() {
  state_.assign(2 * 128, 0.0f);
  context_.assign(64, 0.0f);
}

float SileroVad::Probability(const float* frame) {
  std::vector<float> input(context_);
  input.insert(input.end(), frame, frame + Frame);
  const int64_t rate = TurnSampleRate;
  std::vector<std::vector<float>> outputs;
  std::string error;
  if (!model_->Run({{"input", {1, int64_t(input.size())}, input.data()},
                    {"state", {2, 1, 128}, state_.data()},
                    {"sr", {}, nullptr, &rate}}, outputs, error) ||
      outputs.size() < 2 || outputs[0].empty() || outputs[1].size() != state_.size()) return -1;
  state_ = std::move(outputs[1]);
  context_.assign(input.end() - 64, input.end());
  return outputs[0][0];
}

SmartTurn::SmartTurn(std::unique_ptr<OnnxModel> model) : model_(std::move(model)) {}
SmartTurn::~SmartTurn() = default;

float SmartTurn::Probability(const std::vector<int16_t>& audio) {
  std::vector<float> window(WindowSamples, 0.0f);
  const size_t used = std::min(audio.size(), WindowSamples);
  for (size_t i = 0; i < used; ++i)
    window[WindowSamples - used + i] = audio[audio.size() - used + i] / 32768.0f;
  const auto features = WhisperFeatures(window);
  std::vector<std::vector<float>> outputs;
  std::string error;
  if (!model_->Run({{"input_features", {1, Mels, Frames}, features.data()}}, outputs, error) ||
      outputs.empty() || outputs[0].empty()) return -1;
  return outputs[0][0];
}

TurnDetector::TurnDetector(Vad vad, Endpoint endpoint, Options options)
    : vad_(std::move(vad)), endpoint_(std::move(endpoint)), options_(options) {
  const double frameSecs = double(SileroVad::Frame) / TurnSampleRate;
  startFrames_ = std::max(1, int(std::lround(options_.startSecs / frameSecs)));
  stopFrames_ = std::max(1, int(std::lround(options_.stopSecs / frameSecs)));
}

void TurnDetector::Reset() {
  state_ = State::Quiet;
  count_ = 0;
  pending_.clear();
  recent_.clear();
  turn_.clear();
  inTurn_ = paused_ = false;
  silentSamples_ = 0;
  processed_ = turnStart_ = 0;
}

std::vector<TurnDetector::Event> TurnDetector::Process(const int16_t* samples, size_t count) {
  std::vector<Event> events;
  pending_.insert(pending_.end(), samples, samples + count);
  size_t offset = 0;
  for (; pending_.size() - offset >= SileroVad::Frame; offset += SileroVad::Frame)
    Frame(pending_.data() + offset, events);
  pending_.erase(pending_.begin(), pending_.begin() + offset);
  return events;
}

void TurnDetector::Frame(const int16_t* frame, std::vector<Event>& events) {
  float samples[SileroVad::Frame];
  double energy = 0;
  for (size_t i = 0; i < SileroVad::Frame; ++i) {
    samples[i] = frame[i] / 32768.0f;
    energy += double(samples[i]) * samples[i];
  }
  const double level = 10 * std::log10(energy / SileroVad::Frame + 1e-12);
  const bool speech = vad_(samples) >= options_.confidence && level >= options_.minLevelDbfs;

  processed_ += SileroVad::Frame;
  recent_.insert(recent_.end(), frame, frame + SileroVad::Frame);
  const size_t keep = size_t((options_.preSpeechSecs + options_.startSecs) * TurnSampleRate) + SileroVad::Frame;
  while (recent_.size() > keep) recent_.pop_front();
  if (inTurn_) {
    turn_.insert(turn_.end(), frame, frame + SileroVad::Frame);
    // Smart Turn only reads the end of the turn; bound memory for long turns.
    const size_t limit = size_t(options_.windowSecs * TurnSampleRate) * 2;
    if (turn_.size() > limit) turn_.erase(turn_.begin(), turn_.end() - limit / 2);
  }

  const State before = state_;
  if (speech) {
    if (state_ == State::Quiet) { state_ = State::Starting; count_ = 1; }
    else if (state_ == State::Starting) ++count_;
    else if (state_ == State::Stopping) { state_ = State::Speaking; count_ = 0; }
  } else {
    if (state_ == State::Starting) { state_ = State::Quiet; count_ = 0; }
    else if (state_ == State::Speaking) { state_ = State::Stopping; count_ = 1; }
    else if (state_ == State::Stopping) ++count_;
  }
  if (state_ == State::Starting && count_ >= startFrames_) { state_ = State::Speaking; count_ = 0; }
  if (state_ == State::Stopping && count_ >= stopFrames_) { state_ = State::Quiet; count_ = 0; }

  if (state_ == State::Speaking && before == State::Starting) {
    silentSamples_ = 0;
    if (!inTurn_) {
      inTurn_ = true;
      turn_.assign(recent_.begin(), recent_.end());
      turnStart_ = processed_ - turn_.size();
      events.push_back(Event::SpeechStarted);
    } else if (paused_) {
      paused_ = false;
      events.push_back(Event::SpeechResumed);
    }
    return;
  }
  if (!inTurn_) return;
  if (state_ == State::Quiet && before == State::Stopping) {
    paused_ = true;
    silentSamples_ = 0;
    events.push_back(Event::SpeechPaused);
    probability_ = endpoint_ ? endpoint_(turn_) : 1.0f;
    if (probability_ >= options_.completeProbability) {
      inTurn_ = paused_ = false;
      events.push_back(Event::TurnComplete);
    }
    return;
  }
  if (paused_ && state_ == State::Quiet) {
    silentSamples_ += SileroVad::Frame;
    if (silentSamples_ >= options_.silenceSecs * TurnSampleRate) {
      inTurn_ = paused_ = false;
      events.push_back(Event::TurnComplete);
    }
  }
}
}  // namespace Voice

namespace Voice {
TurnAnalyzer::TurnAnalyzer(std::unique_ptr<TurnDetector> detector, bool threaded) : detector_(std::move(detector)) {
  if (!threaded) return;
  worker_ = std::thread([this] {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
      wake_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
      if (stopping_) return;
      auto pcm = std::move(queue_.front());
      queue_.pop_front();
      const auto resets = resets_;
      lock.unlock();
      auto results = Analyze(pcm);
      lock.lock();
      // A Reset() while analysing makes these results stale.
      if (resets == resets_) results_.insert(results_.end(), results.begin(), results.end());
    }
  });
}

TurnAnalyzer::~TurnAnalyzer() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  wake_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void TurnAnalyzer::Push(const std::string& pcm) {
  if (pcm.empty()) return;
  if (!worker_.joinable()) {
    const auto results = Analyze(pcm);
    results_.insert(results_.end(), results.begin(), results.end());
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(pcm);
  }
  wake_.notify_all();
}

void TurnAnalyzer::Reset() {
  if (!worker_.joinable()) { detector_->Reset(); results_.clear(); return; }
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.clear();
  queue_.push_back({});
  results_.clear();
  ++resets_;
  wake_.notify_all();
}

std::vector<TurnAnalyzer::Result> TurnAnalyzer::Analyze(const std::string& pcm) {
  std::vector<Result> results;
  if (pcm.empty()) {
    detector_->Reset();
    return results;
  }
  std::vector<int16_t> samples(pcm.size() / 2);
  std::memcpy(samples.data(), pcm.data(), samples.size() * sizeof(int16_t));
  for (const auto event : detector_->Process(samples.data(), samples.size()))
    results.push_back({event, detector_->TurnStart(), detector_->LastProbability()});
  return results;
}

std::vector<TurnAnalyzer::Result> TurnAnalyzer::Poll() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Result> results;
  results.swap(results_);
  return results;
}
}  // namespace Voice
