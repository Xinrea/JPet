// Opt-in tool for local speech synthesis (Qwen3-TTS through qwentts.cpp); it
// needs the downloaded synthesis model, so it is not a CTest test.
//
//   voice_synthesis_probe reference <model dir> <24 kHz mono wav> <transcript> <source> <out.json>
//     Extracts the voice reference bundled as resources/models/joi-voice.json.
//   voice_synthesis_probe <model dir> <voice.json> [wav output dir]
//     Speaks JPet-style replies through the pipeline's synthesizer and reports
//     the time to first audio and the real-time factor of each sentence.
#include "VoiceModels.hpp"
#include "VoiceSynthesis.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

Voice::SynthesisModel Model(const std::string& directory) {
  const auto& files = Voice::SpeechModelStore::SynthesisFiles();
  return {directory + "/" + files[0].name, directory + "/" + files[1].name};
}

// 16-bit PCM mono WAV at 24 kHz.
bool ReadWav(const std::string& path, std::vector<float>& audio) {
  std::ifstream file(path, std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(file)), {});
  uint16_t channels = 0, bits = 0;
  uint32_t rate = 0;
  for (size_t at = 12; at + 8 <= bytes.size();) {
    uint32_t size;
    std::memcpy(&size, bytes.data() + at + 4, 4);
    const auto id = bytes.substr(at, 4);
    const char* body = bytes.data() + at + 8;
    if (id == "fmt " && size >= 16) {
      std::memcpy(&channels, body + 2, 2);
      std::memcpy(&rate, body + 4, 4);
      std::memcpy(&bits, body + 14, 2);
    } else if (id == "data" && channels == 1 && bits == 16 && rate == 24000 && at + 8 + size <= bytes.size()) {
      audio.resize(size / 2);
      for (size_t i = 0; i < audio.size(); ++i) {
        int16_t sample;
        std::memcpy(&sample, body + 2 * i, 2);
        audio[i] = sample / 32768.0f;
      }
      return !audio.empty();
    }
    at += 8 + size + (size & 1);
  }
  return false;
}

void WriteWav(const std::string& path, const std::string& pcm) {
  std::ofstream file(path, std::ios::binary);
  const uint32_t data = static_cast<uint32_t>(pcm.size()), riff = 36 + data, fmt = 16, rate = 24000, byteRate = 48000;
  const uint16_t format = 1, channels = 1, align = 2, bits = 16;
  file.write("RIFF", 4).write(reinterpret_cast<const char*>(&riff), 4).write("WAVEfmt ", 8);
  file.write(reinterpret_cast<const char*>(&fmt), 4).write(reinterpret_cast<const char*>(&format), 2);
  file.write(reinterpret_cast<const char*>(&channels), 2).write(reinterpret_cast<const char*>(&rate), 4);
  file.write(reinterpret_cast<const char*>(&byteRate), 4).write(reinterpret_cast<const char*>(&align), 2);
  file.write(reinterpret_cast<const char*>(&bits), 2).write("data", 4).write(reinterpret_cast<const char*>(&data), 4);
  file.write(pcm.data(), static_cast<std::streamsize>(pcm.size()));
}

int Reference(char** argv) {
  std::vector<float> audio;
  if (!ReadWav(argv[3], audio)) { std::cerr << "expected a 24 kHz mono 16-bit WAV\n"; return 1; }
  Voice::VoiceReference voice;
  std::string error;
  if (!Voice::ExtractVoiceReference(Model(argv[2]), audio, argv[4], voice, error)) { std::cerr << error << '\n'; return 1; }
  std::ofstream(argv[6], std::ios::binary) << Voice::VoiceReferenceJson(voice, argv[5]);
  std::printf("speaker %zu values, codes %d x %d\n", voice.speaker.size(), voice.codebooks, voice.frames);
  return 0;
}

int Probe(const std::string& directory, const std::string& voicePath, const std::string& outDir) {
  Voice::VoiceReference voice;
  std::string error;
  if (!Voice::LocalSynthesisAvailable(error) || !Voice::LoadVoiceReference(voicePath, voice, error)) {
    std::cerr << error << '\n';
    return 1;
  }
  const std::vector<std::string> sentences = {
    "好的，音量已经调到30了。", "现在你有1250点经验，可以给智力升两级。", "读书任务已经加入队列，大概还要25分钟。",
    "我在B站帮你搜了千问教程，第一个视频播放量最高。", "嗯，让我想想。", "好呀。",
    "Python和JavaScript都可以，看你更熟悉哪个。", "晚安，明天见！"};
  auto synthesizer = Voice::MakeLocalSynthesizer(Model(directory), std::move(voice),
      [](const std::string& line) { std::fprintf(stderr, "%s\n", line.c_str()); });
  double audioTotal = 0, timeTotal = 0;
  int failures = 0;
  for (size_t i = 0; i < sentences.size(); ++i) {
    const auto start = Clock::now();
    synthesizer->Speak(i + 1, sentences[i]);
    std::string pcm;
    double first = -1;
    for (bool done = false; !done;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      for (auto& output : synthesizer->Poll()) {
        if (!output.pcm.empty() && first < 0) first = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        pcm += output.pcm;
        if (output.done) { done = true; if (!output.error.empty()) { ++failures; std::cerr << output.error << '\n'; } }
      }
    }
    const double seconds = std::chrono::duration<double>(Clock::now() - start).count(), audio = pcm.size() / 2 / 24000.0;
    // The first sentence also waits for the model load and warm-up.
    std::printf("%zu first_audio_ms=%.0f total_ms=%.0f audio_s=%.2f rtf=%.2f%s\n", i + 1, first, seconds * 1000, audio,
                audio > 0 ? seconds / audio : 0.0, i == 0 ? " (includes load)" : "");
    if (i > 0) { audioTotal += audio; timeTotal += seconds; }
    if (!outDir.empty()) WriteWav(outDir + "/" + std::to_string(i + 1) + ".wav", pcm);
  }
  std::printf("rtf after the first sentence=%.2f, failures=%d\n", audioTotal > 0 ? timeTotal / audioTotal : 0.0, failures);
  return failures ? 1 : 0;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc == 7 && std::string(argv[1]) == "reference") return Reference(argv);
  if (argc == 3 || argc == 4) return Probe(argv[1], argv[2], argc == 4 ? argv[3] : "");
  std::cerr << "usage: voice_synthesis_probe reference <model dir> <wav> <transcript> <source> <out.json>\n"
               "       voice_synthesis_probe <model dir> <voice.json> [wav output dir]\n";
  return 2;
}
