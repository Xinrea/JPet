#pragma once

#import <AVFoundation/AVFoundation.h>

namespace Voice {
inline AVAudioConverter* MakeInputConverter(AVAudioFormat* hardware) {
  AVAudioFormat* target = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatInt16
      sampleRate:16000 channels:1 interleaved:YES];
  AVAudioConverter* converter = [[AVAudioConverter alloc] initFromFormat:hardware toFormat:target];
  // USB interfaces expose independent/discrete inputs, not speaker positions.
  // The default layout mapping can map all of them to silence for a mono output.
  // Select the first microphone input explicitly; do not mix in loopback inputs.
  converter.channelMap = @[@0];
  return converter;
}
}  // namespace Voice
