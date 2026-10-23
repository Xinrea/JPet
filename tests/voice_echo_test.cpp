#include "VoiceEcho.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void Check(bool condition, const char* description) {
  if (!condition) throw std::runtime_error(description);
}

// Speech-like test signal: syllable-length bursts of noise band-limited to
// 6 kHz at 48 kHz, so it decimates exactly to the 24 kHz render and 16 kHz
// capture rates.
std::vector<double> Speech(size_t samples, uint32_t seed) {
  std::vector<double> noise(samples);
  for (auto& value : noise) {
    seed = seed * 1664525u + 1013904223u;
    value = static_cast<double>(seed >> 8) / (1u << 24) * 2 - 1;
  }
  constexpr int Taps = 129;
  constexpr double Pi = 3.14159265358979323846, Cutoff = 6000.0 / 48000;
  std::vector<double> filter(Taps);
  for (int i = 0; i < Taps; ++i) {
    const double x = i - (Taps - 1) / 2.0;
    const double sinc = x == 0 ? 2 * Cutoff : std::sin(2 * Pi * Cutoff * x) / (Pi * x);
    filter[i] = sinc * (0.54 - 0.46 * std::cos(2 * Pi * i / (Taps - 1)));
  }
  std::vector<double> out(samples);
  for (size_t n = Taps; n < samples; ++n) {
    double sum = 0;
    for (int i = 0; i < Taps; ++i) sum += filter[i] * noise[n - i];
    // 250 ms syllables separated by 100 ms pauses.
    const double phase = std::fmod(n / 48000.0, 0.35);
    out[n] = phase < 0.25 ? sum * std::sin(Pi * phase / 0.25) : 0;
  }
  return out;
}

int16_t Sample(double value) { return static_cast<int16_t>(std::clamp(value, -32768.0, 32767.0)); }

double Energy(const std::vector<int16_t>& pcm, size_t begin, size_t end) {
  double sum = 1;
  for (size_t i = begin; i < end; ++i) sum += double(pcm[i]) * pcm[i];
  return sum;
}
}  // namespace

int main() {
  try {
    constexpr size_t Seconds = 12, Steps = Seconds * 100;
    constexpr size_t Echo = Voice::EchoCanceller::CaptureRate * 80 / 1000;  // 80 ms acoustic delay.
    const auto far = Speech(Seconds * 48000, 1), near = Speech(Seconds * 48000, 7);
    std::vector<int16_t> render(Seconds * 24000), far16(Seconds * 16000), mic(Seconds * 16000);
    for (size_t i = 0; i < render.size(); ++i) render[i] = Sample(far[i * 2] * 12000);
    for (size_t i = 0; i < far16.size(); ++i) far16[i] = Sample(far[i * 3] * 12000);
    // 0-8 s: far end only; 8-10 s: near end only; 10-12 s: both talk.
    for (size_t n = 0; n < mic.size(); ++n) {
      const double t = double(n) / Voice::EchoCanceller::CaptureRate;
      double value = 0;
      if (t < 8 || t >= 10) {
        // Room echo: direct path and two reflections, attenuated by the speaker volume.
        if (n >= Echo) value += 0.5 * far16[n - Echo];
        if (n >= Echo + 37) value += 0.2 * far16[n - Echo - 37];
        if (n >= Echo + 90) value += 0.08 * far16[n - Echo - 90];
      }
      if (t >= 8) value += near[n * 3] * 6000;
      mic[n] = Sample(value);
    }

    Voice::EchoCanceller echo;
    std::vector<int16_t> cleaned;
    for (size_t step = 0; step < Steps; ++step) {
      // Odd chunk sizes check the 10 ms framing on both directions.
      const size_t r0 = step * 240, c0 = step * 160;
      echo.Render(render.data() + r0, 100);
      echo.Render(render.data() + r0 + 100, 140);
      std::string pcm(reinterpret_cast<const char*>(mic.data() + c0), 160 * sizeof(int16_t));
      const auto out = echo.Capture(pcm.substr(0, 90)) + echo.Capture(pcm.substr(90));
      Check(out.size() == pcm.size(), "complete frames are returned once buffered");
      const size_t at = cleaned.size();
      cleaned.resize(at + out.size() / 2);
      std::memcpy(cleaned.data() + at, out.data(), out.size());
    }
    const auto rate = Voice::EchoCanceller::CaptureRate;
    const double erle = 10 * std::log10(Energy(mic, 6 * rate, 8 * rate) / Energy(cleaned, 6 * rate, 8 * rate));
    const double nearOnly = 10 * std::log10(Energy(cleaned, 8 * rate + rate / 2, 10 * rate) / Energy(mic, 8 * rate + rate / 2, 10 * rate));
    std::vector<int16_t> nearPcm(mic.size());
    for (size_t n = 0; n < mic.size(); ++n) nearPcm[n] = Sample(n >= 8 * rate ? near[n * 3] * 6000 : 0);
    const double doubleTalk = 10 * std::log10(Energy(cleaned, 10 * rate + rate / 2, 12 * rate) / Energy(nearPcm, 10 * rate + rate / 2, 12 * rate));
    std::cout << "echo suppression " << erle << " dB, near-end level " << nearOnly << " dB, double talk "
              << doubleTalk << " dB, " << echo.Summary() << '\n';
    Check(erle > 20, "converged AEC3 removes far-end echo");
    Check(nearOnly > -6, "near-end speech passes without far-end audio");
    Check(doubleTalk > -10 && doubleTalk < 6, "near-end speech survives double talk");

    // Control: without the speaker reference only noise suppression acts on the echo.
    Voice::EchoCanceller blind;
    const std::vector<int16_t> silence(240);
    std::vector<int16_t> unreferenced;
    for (size_t step = 0; step < 800; ++step) {
      blind.Render(silence.data(), silence.size());
      const auto out = blind.Capture(std::string(reinterpret_cast<const char*>(mic.data() + step * 160), 320));
      const size_t at = unreferenced.size();
      unreferenced.resize(at + out.size() / 2);
      std::memcpy(unreferenced.data() + at, out.data(), out.size());
    }
    const double control = 10 * std::log10(Energy(mic, 6 * rate, 8 * rate) / Energy(unreferenced, 6 * rate, 8 * rate));
    std::cout << "without reference " << control << " dB\n";
    Check(control < 10, "echo removal depends on the speaker reference");

    echo.Capture(std::string(5, '\0'));
    echo.EndCapture();
    Check(echo.Capture(std::string(320, '\0')).size() == 320, "a new microphone session starts frame-aligned");
    std::cout << "PASS voice echo cancellation\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
