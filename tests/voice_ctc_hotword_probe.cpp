// Opt-in only. See README-voice-ctc-hotwords.md for the custom-runtime build.
#include <sherpa-onnx/c-api/c-api.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
double Milliseconds(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::string Json(const std::string& text) {
  std::string out = "\"";
  constexpr char hex[] = "0123456789abcdef";
  for (const unsigned char c : text) {
    if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
    else if (c < 0x20) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
    else out += char(c);
  }
  return out + '"';
}

// Parse RIFF chunks, not a fixed 44-byte header or a search for the word "data".
// Deliberately accepts only uncompressed, little-endian, 16 kHz mono PCM16.
std::vector<float> ReadWav(const std::string& path) {
  const auto fail = [&path](const char* why) { throw std::runtime_error(path + ": " + why); };
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) fail("cannot open WAV");
  const auto size = file.tellg();
  if (size < 12 || size > 128 * 1024 * 1024) fail("WAV size outside 12 B..128 MiB");
  std::vector<unsigned char> bytes(static_cast<size_t>(size));
  file.seekg(0);
  if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) fail("cannot read WAV");
  const auto tag = [&bytes](size_t p, const char* s) {
    for (size_t i = 0; i < 4; ++i) if (bytes[p + i] != static_cast<unsigned char>(s[i])) return false;
    return true;
  };
  const auto u16 = [&bytes](size_t p) { return uint32_t(bytes[p]) | (uint32_t(bytes[p + 1]) << 8); };
  const auto u32 = [&u16](size_t p) { return u16(p) | (u16(p + 2) << 16); };
  if (!tag(0, "RIFF") || !tag(8, "WAVE")) fail("expected RIFF/WAVE");
  if (uint64_t(u32(4)) + 8 != bytes.size()) fail("RIFF length does not match file");
  bool haveFormat = false, haveData = false;
  size_t dataOffset = 0, dataSize = 0;
  for (size_t p = 12; p < bytes.size();) {
    if (bytes.size() - p < 8) fail("truncated chunk header");
    const size_t length = u32(p + 4), begin = p + 8;
    if (length > bytes.size() - begin) fail("truncated chunk payload");
    const size_t end = begin + length;
    if ((length & 1) && end == bytes.size()) fail("missing chunk padding");
    if (tag(p, "fmt ")) {
      if (haveFormat || length < 16) fail("duplicate or short fmt chunk");
      haveFormat = true;
      if (u16(begin) != 1 || u16(begin + 2) != 1 || u32(begin + 4) != 16000 ||
          u32(begin + 8) != 32000 || u16(begin + 12) != 2 || u16(begin + 14) != 16)
        fail("expected 16 kHz mono PCM16 (format=1, byte_rate=32000, block_align=2)");
    } else if (tag(p, "data")) {
      if (haveData) fail("duplicate data chunk");
      haveData = true;
      dataOffset = begin;
      dataSize = length;
    }
    p = end + (length & 1);
  }
  if (!haveFormat || !haveData || dataSize == 0 || dataSize % 2) fail("missing fmt/data or empty/odd PCM payload");
  std::vector<float> samples(dataSize / 2);
  for (size_t i = 0; i < samples.size(); ++i) {
    const int32_t value = static_cast<int32_t>(u16(dataOffset + 2 * i));
    samples[i] = (value >= 32768 ? value - 65536 : value) / 32768.0f;
  }
  return samples;
}

using Recognizer = std::unique_ptr<const SherpaOnnxOfflineRecognizer, decltype(&SherpaOnnxDestroyOfflineRecognizer)>;
using Stream = std::unique_ptr<const SherpaOnnxOfflineStream, decltype(&SherpaOnnxDestroyOfflineStream)>;
using Result = std::unique_ptr<const SherpaOnnxOfflineRecognizerResult, decltype(&SherpaOnnxDestroyOfflineRecognizerResult)>;

struct Transcript { std::string text; double decodeMs; };
Transcript Decode(const SherpaOnnxOfflineRecognizer* recognizer, const std::vector<float>& audio) {
  Stream stream(SherpaOnnxCreateOfflineStream(recognizer), SherpaOnnxDestroyOfflineStream);
  if (!stream) throw std::runtime_error("cannot create stream");
  SherpaOnnxAcceptWaveformOffline(stream.get(), 16000, audio.data(), static_cast<int32_t>(audio.size()));
  const auto start = Clock::now();
  SherpaOnnxDecodeOfflineStream(recognizer, stream.get());
  const double elapsed = Milliseconds(start);
  Result result(SherpaOnnxGetOfflineStreamResult(stream.get()), SherpaOnnxDestroyOfflineRecognizerResult);
  if (!result || !result->text) throw std::runtime_error("recognizer returned no result");
  return {result->text, elapsed};
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 3 && std::string(argv[1]) == "--check-wav") {
      const auto samples = ReadWav(argv[2]);
      std::cout << "{\"samples\":" << samples.size() << "}\n";
      return 0;
    }
    if (argc < 5) {
      std::cerr << "Usage: voice_ctc_hotword_probe MODEL_DIR HOTWORDS_FILE SCORE WAV [WAV ...]\n"
                   "       voice_ctc_hotword_probe --check-wav WAV\n";
      return 2;
    }
    size_t consumed = 0;
    const float score = std::stof(argv[3], &consumed);
    if (consumed != std::string(argv[3]).size() || !std::isfinite(score))
      throw std::runtime_error("SCORE must be a finite float");
    const std::string model = std::string(argv[1]) + "/model.int8.onnx";
    const std::string tokens = std::string(argv[1]) + "/tokens.txt";
    for (const auto& path : {model, tokens, std::string(argv[2])})
      if (!std::ifstream(path).good()) throw std::runtime_error("cannot open " + path);
    auto audio = ReadWav(argv[4]);
    const std::array<const char*, 3> names = {"greedy", "beam", "hotwords"};
    std::vector<Recognizer> recognizers;
    std::cout << std::setprecision(10);
    for (size_t i = 0; i < names.size(); ++i) {
      SherpaOnnxOfflineRecognizerConfig config{};
      config.feat_config.sample_rate = 16000;
      config.feat_config.feature_dim = 80;
      config.model_config.sense_voice.model = model.c_str();
      config.model_config.sense_voice.language = "auto";
      config.model_config.sense_voice.use_itn = 1;
      config.model_config.tokens = tokens.c_str();
      config.model_config.num_threads = 2;
      config.model_config.provider = "cpu";
      config.decoding_method = i == 0 ? "greedy_search" : "jpet_ctc_beam_search";
      config.max_active_paths = 8;
      if (i == 2) { config.hotwords_file = argv[2]; config.hotwords_score = score; }
      const auto start = Clock::now();
      Recognizer recognizer(SherpaOnnxCreateOfflineRecognizer(&config), SherpaOnnxDestroyOfflineRecognizer);
      if (!recognizer) throw std::runtime_error(std::string("cannot create ") + names[i] + " recognizer (custom runtime required)");
      const double loadMs = Milliseconds(start);
      const auto warmup = Decode(recognizer.get(), audio);
      std::cout << "{\"type\":\"warmup\",\"config\":" << Json(names[i])
                << ",\"method\":" << Json(config.decoding_method)
                << ",\"load_ms\":" << loadMs << ",\"decode_ms\":" << warmup.decodeMs << "}\n";
      recognizers.push_back(std::move(recognizer));
    }
    // All three recognizers are warm before any measured decode. Each sample is
    // read once, then the exact same float buffer is passed to all three.
    for (int index = 4; index < argc; ++index) {
      if (index != 4) audio = ReadWav(argv[index]);
      // Rotate order to reduce systematic warm-cache/thermal ordering bias.
      for (size_t offset = 0; offset < names.size(); ++offset) {
        const size_t i = (static_cast<size_t>(index - 4) + offset) % names.size();
        const auto transcript = Decode(recognizers[i].get(), audio);
        std::cout << "{\"type\":\"result\",\"input_index\":" << index - 4
                  << ",\"config\":" << Json(names[i]) << ",\"wav\":" << Json(argv[index])
                  << ",\"audio_seconds\":" << audio.size() / 16000.0
                  << ",\"decode_ms\":" << transcript.decodeMs << ",\"text\":" << Json(transcript.text) << "}\n";
      }
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "voice_ctc_hotword_probe: " << error.what() << '\n';
    return 1;
  }
}
