#include "VoiceAudioMac.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void CheckConversion(double rate, unsigned channels, bool interleaved, unsigned active) {
  AVAudioChannelLayout* layout = [[AVAudioChannelLayout alloc]
      initWithLayoutTag:kAudioChannelLayoutTag_DiscreteInOrder | channels];
  AVAudioFormat* input = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32
      sampleRate:rate interleaved:interleaved channelLayout:layout];
  AVAudioConverter* converter = Voice::MakeInputConverter(input);
  Check(converter != nil, "microphone converter can be created");
  AVAudioFormat* target = converter.outputFormat;
  Check(target.sampleRate == 16000 && target.channelCount == 1 &&
      target.commonFormat == AVAudioPCMFormatInt16 && target.isInterleaved,
      "output is 16 kHz mono signed 16-bit PCM");

  size_t samples = 0, crossings = 0;
  double energy = 0;
  int previous = 0;
  for (unsigned offset = 0; offset < rate; offset += 1024) {
    const unsigned frames = std::min(1024u, unsigned(rate) - offset);
    AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:input frameCapacity:frames];
    buffer.frameLength = frames;
    for (unsigned channel = 0; channel < channels; ++channel) {
      for (unsigned frame = 0; frame < frames; ++frame) {
        const float value = channel == active ?
            0.5 * std::sin(2 * M_PI * 1000 * (offset + frame) / rate) : 0;
        if (interleaved) buffer.floatChannelData[0][frame * channels + channel] = value;
        else buffer.floatChannelData[channel][frame] = value;
      }
    }
    AVAudioPCMBuffer* output = [[AVAudioPCMBuffer alloc] initWithPCMFormat:target
        frameCapacity:std::ceil(frames * 16000.0 / rate) + 32];
    __block BOOL supplied = NO;
    NSError* error = nil;
    const auto result = [converter convertToBuffer:output error:&error withInputFromBlock:
        ^AVAudioBuffer*(AVAudioPacketCount, AVAudioConverterInputStatus* status) {
          if (supplied) { *status = AVAudioConverterInputStatus_NoDataNow; return nil; }
          supplied = YES;
          *status = AVAudioConverterInputStatus_HaveData;
          return buffer;
        }];
    Check(result != AVAudioConverterOutputStatus_Error && error == nil, "stream conversion succeeds");
    for (unsigned frame = 0; frame < output.frameLength; ++frame) {
      const int sample = output.int16ChannelData[0][frame];
      energy += double(sample) * sample;
      if (previous < 0 && sample >= 0) ++crossings;
      previous = sample;
      ++samples;
    }
  }
  Check(samples >= 15980 && samples <= 16000, "one second converts to one second at 16 kHz");
  const double rms = std::sqrt(energy / samples);
  if (active == 0) {
    Check(rms > 11000 && rms < 12000, "discrete microphone input remains audible after conversion");
    Check(std::abs(crossings * 16000.0 / samples - 1000) < 5, "resampling preserves pitch and speed");
  } else {
    Check(rms == 0, "other interface inputs and loopback do not enter microphone audio");
  }
}
}  // namespace

int main() {
  @autoreleasepool {
    try {
      for (double rate : {16000.0, 44100.0, 48000.0}) {
        for (unsigned channels : {1u, 2u, 4u}) {
          for (bool interleaved : {false, true}) {
            for (unsigned active = 0; active < channels; ++active)
              CheckConversion(rate, channels, interleaved, active);
          }
        }
      }
      std::cout << "PASS native microphone conversion: discrete channels, PCM format, level, duration and pitch\n";
    } catch (const std::exception& error) {
      std::cerr << "FAIL: " << error.what() << '\n';
      return 1;
    }
  }
}
