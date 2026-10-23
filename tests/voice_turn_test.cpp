#include "VoiceOnnx.hpp"
#include "VoiceTurn.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Voice::TurnDetector;
using Event = TurnDetector::Event;

void Check(bool condition, const char* description) {
  if (!condition) throw std::runtime_error(description);
}

std::vector<int16_t> Tone(double seconds, double amplitude = 8000, double hertz = 220) {
  std::vector<int16_t> pcm(size_t(seconds * Voice::TurnSampleRate));
  for (size_t i = 0; i < pcm.size(); ++i)
    pcm[i] = int16_t(amplitude * std::sin(2 * 3.14159265358979 * hertz * i / Voice::TurnSampleRate));
  return pcm;
}

std::vector<Event> Feed(TurnDetector& detector, const std::vector<int16_t>& pcm) {
  std::vector<Event> events;
  // Odd chunk sizes exercise the 512-sample framing.
  for (size_t offset = 0; offset < pcm.size(); offset += 333) {
    const auto produced = detector.Process(pcm.data() + offset, std::min<size_t>(333, pcm.size() - offset));
    events.insert(events.end(), produced.begin(), produced.end());
  }
  return events;
}

// Scripted VAD: any audible frame is speech.
float LoudVad(const float* frame) {
  double energy = 0;
  for (size_t i = 0; i < Voice::SileroVad::Frame; ++i) energy += frame[i] * frame[i];
  return energy > 1e-4 ? 0.95f : 0.05f;
}

void StateMachine() {
  float endpoint = 0.9f;
  int endpointCalls = 0;
  TurnDetector detector(LoudVad, [&](const std::vector<int16_t>&) { ++endpointCalls; return endpoint; });
  auto events = Feed(detector, Tone(1, 0));
  Check(events.empty() && !detector.InTurn(), "silence does not start a turn");
  events = Feed(detector, Tone(0.1));
  Check(events.empty(), "speech shorter than start_secs is ignored");
  Feed(detector, Tone(0.5, 0));
  events = Feed(detector, Tone(1.0));
  Check(events == std::vector<Event>{Event::SpeechStarted} && detector.InTurn(), "confirmed speech starts a turn");
  Check(detector.TurnAudio().size() >= size_t(0.65 * Voice::TurnSampleRate), "turn audio keeps the pre-speech buffer");
  events = Feed(detector, Tone(0.5, 0));
  Check((events == std::vector<Event>{Event::SpeechPaused, Event::TurnComplete}) && endpointCalls == 1 && !detector.InTurn(),
      "a pause Smart Turn accepts completes the turn");

  endpoint = 0.1f;
  Feed(detector, Tone(1.0));
  events = Feed(detector, Tone(0.5, 0));
  Check(events == std::vector<Event>{Event::SpeechPaused} && detector.InTurn(), "an unfinished turn waits after a pause");
  events = Feed(detector, Tone(0.6));
  Check(events == std::vector<Event>{Event::SpeechResumed}, "speaking again resumes the same turn");
  events = Feed(detector, Tone(1.0, 0));
  Check(events == std::vector<Event>{Event::SpeechPaused}, "the second pause is still unfinished");
  events = Feed(detector, Tone(2.5, 0));
  Check(events == std::vector<Event>{Event::TurnComplete} && !detector.InTurn(), "a long silence ends the turn anyway");

  events = Feed(detector, Tone(1.0, 3));  // About -80 dBFS.
  Check(events.empty(), "quiet frames below the volume gate are not speech");
  detector.Reset();
  Check(!detector.InTurn() && detector.TurnAudio().empty(), "reset clears the turn");
}

void Features() {
  std::vector<float> tone(Voice::TurnSampleRate * 2);
  for (size_t i = 0; i < tone.size(); ++i) tone[i] = 0.3f * float(std::sin(2 * 3.14159265358979 * 1000 * i / 16000.0));
  const auto features = Voice::WhisperFeatures(tone);
  Check(features.size() == 80 * 800, "Smart Turn input is 80 x 800");
  const auto [low, high] = std::minmax_element(features.begin(), features.end());
  Check(*high - *low <= 2.0f + 1e-5f, "log-mel range is clamped to 8 decades");
  // A 1 kHz tone peaks in the mel band around 1 kHz during the first 2 s only.
  int peak = 0;
  for (int m = 1; m < 80; ++m) if (features[m * 800 + 50] > features[peak * 800 + 50]) peak = m;
  Check(peak >= 24 && peak <= 28, "a 1 kHz tone lands in the expected mel bands");
  Check(features[peak * 800 + 50] > features[peak * 800 + 600], "padding after the audio is quieter");
}

std::vector<int16_t> Speech(const std::string& text, const std::string& directory) {
#ifdef __APPLE__
  const auto aiff = directory + "/speech.aiff", wav = directory + "/speech.wav";
  const auto command = "say -v Tingting -o '" + aiff + "' '" + text + "' && afconvert -f WAVE -d LEI16@16000 -c 1 '" +
      aiff + "' '" + wav + "' 2>/dev/null";
  if (std::system(command.c_str()) != 0) return {};
  std::ifstream file(wav, std::ios::binary);
  std::string bytes((std::istreambuf_iterator<char>(file)), {});
  const auto data = bytes.find("data");
  if (data == std::string::npos) return {};
  std::vector<int16_t> pcm((bytes.size() - data - 8) / 2);
  std::copy_n(bytes.data() + data + 8, pcm.size() * 2, reinterpret_cast<char*>(pcm.data()));
  return pcm;
#else
  (void)text; (void)directory;
  return {};
#endif
}

void Models(const std::string& resources) {
  std::string error;
  auto vadModel = Voice::OnnxModel::Load(resources + "/silero_vad.onnx", error);
  if (!vadModel) { std::cout << "SKIP onnxruntime models: " << error << '\n'; return; }
  auto turnModel = Voice::OnnxModel::Load(resources + "/smart-turn-v3.2-cpu.onnx", error);
  Check(turnModel != nullptr, "Smart Turn model loads");
  Voice::SileroVad vad(std::move(vadModel));
  Voice::SmartTurn smart(std::move(turnModel));
  std::vector<float> quiet(Voice::SileroVad::Frame, 0.0f);
  Check(vad.Probability(quiet.data()) < 0.2f, "Silero hears no speech in silence");

  const char* directory = std::getenv("TMPDIR");
  const auto complete = Speech("今天的天气怎么样？", directory ? directory : "/tmp");
  if (complete.empty()) { std::cout << "SKIP synthetic speech\n"; return; }
  vad.Reset();
  float peak = 0;
  std::vector<float> frame(Voice::SileroVad::Frame);
  for (size_t offset = 0; offset + frame.size() <= complete.size(); offset += frame.size()) {
    for (size_t i = 0; i < frame.size(); ++i) frame[i] = complete[offset + i] / 32768.0f;
    peak = std::max(peak, vad.Probability(frame.data()));
  }
  const auto start = std::chrono::steady_clock::now();
  const float finished = smart.Probability(complete);
  const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  const auto partial = Speech("我想问一下那个就是", directory ? directory : "/tmp");
  const float unfinished = smart.Probability(partial);
  std::cout << "silero peak " << peak << ", smart turn complete " << finished << " / unfinished " << unfinished
            << " (" << elapsed << " ms)\n";
  Check(peak > 0.8f, "Silero detects synthetic speech");
  Check(finished > 0.5f, "Smart Turn accepts a finished question");
  Check(unfinished < finished, "an unfinished phrase scores lower than a finished one");

  // End to end with the real models: speech, then silence.
  TurnDetector detector([&](const float* f) { return vad.Probability(f); },
      [&](const std::vector<int16_t>& audio) { return smart.Probability(audio); });
  vad.Reset();
  auto events = Feed(detector, Tone(0.5, 0));
  auto speech = Feed(detector, complete);
  auto tail = Feed(detector, Tone(1.0, 0));
  speech.insert(speech.end(), tail.begin(), tail.end());
  Check(!speech.empty() && speech.front() == Event::SpeechStarted && speech.back() == Event::TurnComplete,
      "Silero and Smart Turn detect one complete turn");
}
}  // namespace

int main(int argc, char** argv) {
  try {
    StateMachine();
    Features();
    Models(argc > 1 ? argv[1] : "resources/models");
    std::cout << "PASS voice turn detection\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
