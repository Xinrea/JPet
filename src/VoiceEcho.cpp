#include "VoiceEcho.hpp"

#include <modules/audio_processing/include/audio_processing.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace Voice {
struct EchoCanceller::Impl {
  rtc::scoped_refptr<webrtc::AudioProcessing> apm;
  std::mutex renderMutex, captureMutex;
  std::vector<int16_t> render;  // Partial far-end frame.
  std::string capture;          // Partial near-end frame (s16le bytes).
  std::atomic<int> delay{0};
  double input = 0, output = 0;  // Capture energy since the last Summary().
  size_t frames = 0;
};

EchoCanceller::EchoCanceller() : impl_(std::make_unique<Impl>()) {
  webrtc::AudioProcessing::Config config;
  config.echo_canceller.enabled = true;
  config.echo_canceller.mobile_mode = false;
  config.noise_suppression.enabled = true;
  config.noise_suppression.level = webrtc::AudioProcessing::Config::NoiseSuppression::kModerate;
  config.high_pass_filter.enabled = true;
  impl_->apm = webrtc::AudioProcessingBuilder().SetConfig(config).Create();
}

EchoCanceller::~EchoCanceller() = default;

void EchoCanceller::Render(const int16_t* samples, size_t count) {
  constexpr size_t Frame = RenderRate / 100;
  const webrtc::StreamConfig config(RenderRate, 1);
  std::lock_guard<std::mutex> lock(impl_->renderMutex);
  auto& pending = impl_->render;
  pending.insert(pending.end(), samples, samples + count);
  int16_t output[Frame];
  size_t offset = 0;
  for (; pending.size() - offset >= Frame; offset += Frame)
    impl_->apm->ProcessReverseStream(pending.data() + offset, config, config, output);
  pending.erase(pending.begin(), pending.begin() + offset);
}

std::string EchoCanceller::Capture(const std::string& pcm) {
  constexpr size_t Frame = CaptureRate / 100;
  constexpr size_t FrameBytes = Frame * sizeof(int16_t);
  const webrtc::StreamConfig config(CaptureRate, 1);
  std::lock_guard<std::mutex> lock(impl_->captureMutex);
  auto& pending = impl_->capture;
  pending += pcm;
  std::string cleaned;
  int16_t frame[Frame];
  size_t offset = 0;
  for (; pending.size() - offset >= FrameBytes; offset += FrameBytes) {
    std::memcpy(frame, pending.data() + offset, FrameBytes);
    for (const auto sample : frame) impl_->input += double(sample) * sample;
    impl_->apm->set_stream_delay_ms(impl_->delay);
    impl_->apm->ProcessStream(frame, config, config, frame);
    for (const auto sample : frame) impl_->output += double(sample) * sample;
    ++impl_->frames;
    cleaned.append(reinterpret_cast<const char*>(frame), FrameBytes);
  }
  pending.erase(0, offset);
  return cleaned;
}

void EchoCanceller::EndCapture() {
  std::lock_guard<std::mutex> lock(impl_->captureMutex);
  impl_->capture.clear();
}

void EchoCanceller::SetStreamDelay(int milliseconds) { impl_->delay = milliseconds; }

std::string EchoCanceller::Summary() {
  std::lock_guard<std::mutex> lock(impl_->captureMutex);
  const double samples = std::max<double>(1, impl_->frames * (CaptureRate / 100));
  const auto dbfs = [&](double energy) { return 10 * std::log10(energy / samples / (32768.0 * 32768.0) + 1e-12); };
  char text[128];
  std::snprintf(text, sizeof(text), "frames=%zu input=%.1fdBFS output=%.1fdBFS reduction=%.1fdB",
      impl_->frames, dbfs(impl_->input), dbfs(impl_->output), dbfs(impl_->input) - dbfs(impl_->output));
  impl_->input = impl_->output = 0;
  impl_->frames = 0;
  return text;
}
}  // namespace Voice
