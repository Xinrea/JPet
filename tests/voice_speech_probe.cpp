// Opt-in benchmark for local SenseVoice recognition on macOS. Uses the model's
// own test recordings and JPet-style requests spoken by the system voices
// (`say`), clean and with added noise, and reports character error rate (CER)
// and decoding latency. Nothing leaves the machine.
#include "VoiceSpeech.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

namespace {
std::vector<std::string> Characters(const std::string& text) {
  std::vector<std::string> out;
  for (size_t i = 0; i < text.size();) {
    const unsigned char lead = text[i];
    const size_t length = lead < 0x80 ? 1 : (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : 4;
    auto c = text.substr(i, length);
    i += length;
    // Compare content only: drop punctuation, spaces and case.
    if (length == 1 && !std::isalnum(lead)) continue;
    if (length == 3 && (c == "，" || c == "。" || c == "？" || c == "！" || c == "、" || c == "：" || c == "；")) continue;
    if (length == 1) c[0] = static_cast<char>(std::tolower(lead));
    out.push_back(c);
  }
  return out;
}

double Cer(const std::string& reference, const std::string& hypothesis, size_t& length) {
  const auto a = Characters(reference), b = Characters(hypothesis);
  std::vector<size_t> previous(b.size() + 1), current(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j) previous[j] = j;
  for (size_t i = 1; i <= a.size(); ++i) {
    current[0] = i;
    for (size_t j = 1; j <= b.size(); ++j)
      current[j] = std::min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
    std::swap(previous, current);
  }
  length = a.size();
  return double(previous[b.size()]);
}

std::string ReadWav(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  std::string bytes((std::istreambuf_iterator<char>(file)), {});
  const auto data = bytes.find("data");
  return data == std::string::npos ? std::string{} : bytes.substr(data + 8);
}

std::string Say(const std::string& voice, const std::string& text, const std::string& directory) {
  const auto aiff = directory + "/probe.aiff", wav = directory + "/probe.wav";
  const auto command = "say -v '" + voice + "' -o '" + aiff + "' '" + text + "' && afconvert -f WAVE -d LEI16@16000 -c 1 '" +
      aiff + "' '" + wav + "' 2>/dev/null";
  return std::system(command.c_str()) == 0 ? ReadWav(wav) : std::string{};
}

// Adds white noise at the given signal-to-noise ratio and 0.3 s of silence on both sides.
std::string Noisy(const std::string& pcm, double snr, std::mt19937& random) {
  std::vector<int16_t> samples(pcm.size() / 2);
  std::copy_n(pcm.data(), samples.size() * 2, reinterpret_cast<char*>(samples.data()));
  double energy = 1;
  for (const auto s : samples) energy += double(s) * s;
  const double sigma = std::sqrt(energy / samples.size() / std::pow(10, snr / 10));
  std::normal_distribution<double> noise(0, sigma);
  std::vector<int16_t> out(samples.size() + 9600);
  for (size_t i = 0; i < out.size(); ++i) {
    const double clean = i >= 4800 && i - 4800 < samples.size() ? samples[i - 4800] : 0;
    out[i] = int16_t(std::clamp(clean + noise(random), -32768.0, 32767.0));
  }
  return std::string(reinterpret_cast<const char*>(out.data()), out.size() * 2);
}

struct Stats {
  double errors = 0, characters = 0, audio = 0, decode = 0, worst = 0;
  int count = 0;
  void Add(double e, size_t n, double seconds, double milliseconds) {
    errors += e; characters += double(n); audio += seconds; decode += milliseconds; worst = std::max(worst, milliseconds); ++count;
  }
  void Print(const char* name) const {
    std::printf("%-22s n=%-3d CER=%5.1f%%  mean=%6.1f ms  max=%6.1f ms  RTF=%.3f\n", name, count,
        100 * errors / std::max(1.0, characters), decode / std::max(1, count), worst, decode / 1000 / std::max(1e-9, audio));
  }
};
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { std::cerr << "Usage: voice_speech_probe <SenseVoice model directory> [test_wavs directory]\n"; return 2; }
  using Clock = std::chrono::steady_clock;
  const auto ms = [](Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); };
  std::string error;
  auto start = Clock::now();
  auto recognizer = Voice::LocalRecognizer::Load(argv[1], error);
  if (!recognizer) { std::cerr << "load failed: " << error << '\n'; return 1; }
  std::printf("model load %.0f ms\n", ms(start));
  // The first decode pays for lazy initialisation; report it separately.
  start = Clock::now();
  recognizer->Transcribe(std::string(32000, '\0'));
  std::printf("warm-up decode %.0f ms\n\n", ms(start));

  if (argc > 2) {
    for (const char* name : {"zh", "yue", "en", "ja", "ko"}) {
      const auto pcm = ReadWav(std::string(argv[2]) + "/" + name + ".wav");
      if (pcm.empty()) continue;
      start = Clock::now();
      const auto text = recognizer->Transcribe(pcm);
      std::printf("%-3s %4.1f s %6.1f ms  %s\n", name, pcm.size() / 32000.0, ms(start), text.c_str());
    }
    std::printf("\n");
  }

  const std::vector<std::string> requests = {
    "帮我看看现在有多少经验", "把读书加入任务队列", "取消当前的任务", "智力升一点", "搜一下今天的新闻",
    "去B站搜千问教程", "用浏览器打开哔哩哔哩", "把音量调小一点", "关闭鼠标追踪", "换成粉色的衣服",
    "今天天气怎么样", "给我讲个笑话吧", "你觉得我应该先做哪个任务", "等一下，我还没说完", "轴伊你好呀"};
  const std::vector<std::string> voices = {"Tingting", "Eddy (中文（中国大陆）)", "Grandpa (中文（中国大陆）)",
                                           "Sandy (中文（中国大陆）)", "Meijia"};
  const char* directory = std::getenv("TMPDIR");
  std::mt19937 random(7);
  Stats clean, noisy15, noisy5;
  int shown = 0;
  for (const auto& voice : voices) {
    Stats perVoice;
    for (const auto& request : requests) {
      const auto pcm = Say(voice, request, directory ? directory : "/tmp");
      if (pcm.empty()) continue;
      const double seconds = pcm.size() / 32000.0;
      for (auto [variant, stats] : {std::pair{0.0, &clean}, {15.0, &noisy15}, {5.0, &noisy5}}) {
        const auto input = variant ? Noisy(pcm, variant, random) : pcm;
        start = Clock::now();
        const auto text = recognizer->Transcribe(input);
        const double elapsed = ms(start);
        size_t length = 0;
        const double errors = Cer(request, text, length);
        stats->Add(errors, length, input.size() / 32000.0, elapsed);
        if (!variant) perVoice.Add(errors, length, seconds, elapsed);
        if (errors && shown < 12) { ++shown; std::printf("  [%s snr=%s] %s -> %s\n", voice.c_str(), variant ? std::to_string(int(variant)).c_str() : "clean", request.c_str(), text.c_str()); }
      }
    }
    perVoice.Print(voice.substr(0, voice.find(' ')).c_str());
  }
  std::printf("\n");
  clean.Print("all voices, clean");
  noisy15.Print("all voices, 15 dB SNR");
  noisy5.Print("all voices, 5 dB SNR");
  return 0;
}
