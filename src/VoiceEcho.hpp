#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace Voice {
// WebRTC AEC3 with noise suppression and a high-pass filter for the 16 kHz mono
// microphone. The far-end reference is the 24 kHz mono reply PCM exactly as it is
// written to the speaker (after volume), including silence while nothing plays.
// Render and Capture may be called from two different threads.
class EchoCanceller {
 public:
  static constexpr int CaptureRate = 16000;
  static constexpr int RenderRate = 24000;

  EchoCanceller();
  ~EchoCanceller();
  // Feeds speaker samples; partial 10 ms frames wait for the next call.
  void Render(const int16_t* samples, size_t count);
  // Returns cleaned s16le PCM for each complete 10 ms frame and keeps the rest.
  std::string Capture(const std::string& pcm);
  // Drops a partial frame left over from the previous microphone session.
  void EndCapture();
  // Hint for the speaker-to-microphone delay; AEC3 also estimates it itself.
  void SetStreamDelay(int milliseconds);
  // Microphone level before and after processing since the previous call.
  std::string Summary();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace Voice
