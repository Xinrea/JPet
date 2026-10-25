#pragma once

#include <nlohmann/json.hpp>

// Character voices: the startup greeting, idle chatter, and touch reactions.
enum class AudioType { START, IDLE, CLICK };

// The [audio] settings. Volumes are percentages from 0 to 100. Touch (CLICK)
// and idle voices are scaled by the master volume, which alone sets the startup
// greeting and voice chat replies; muting it silences all of them.
struct AudioSettings {
  int volume = 20;
  bool mute = false;
  int touch_volume = 100;
  bool touch_audio = true;  // false mutes touch voices
  int idle_volume = 100;
  bool idle_audio = true;  // false mutes idle voices

  // Playback gains from 0 to 1.
  float MasterGain() const { return mute ? 0.0f : Fraction(volume); }
  float Gain(AudioType type) const {
    switch (type) {
      case AudioType::CLICK: return touch_audio ? MasterGain() * Fraction(touch_volume) : 0.0f;
      case AudioType::IDLE: return idle_audio ? MasterGain() * Fraction(idle_volume) : 0.0f;
      case AudioType::START: break;
    }
    return MasterGain();
  }

 private:
  static float Fraction(int percent) { return percent / 100.0f; }
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AudioSettings, volume, mute, touch_volume, touch_audio, idle_volume, idle_audio)
