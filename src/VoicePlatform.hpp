#pragma once

#include "VoiceIndicatorStyle.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace Voice {
struct Event {
  enum class Type { Microphone, Error, Diagnostic };
  Type type;
  std::string data;
};

// All methods are called on the application thread. Native callbacks only
// enqueue events, so audio callbacks never touch the renderer.
class Platform {
 public:
  virtual ~Platform() = default;
  virtual bool ShortcutHeld() const = 0;
  virtual void StartCapture() = 0;
  virtual void StopCapture() = 0;
  virtual std::vector<Event> Poll() = 0;
  virtual void Play(const std::string& pcm, float volume) = 0;
  virtual bool IsPlaying() const = 0;
  virtual void StopPlayback() = 0;
  // 24 kHz reply samples heard since the last StopPlayback (approximate on
  // devices that buffer ahead).
  virtual uint64_t PlayedSamples() const = 0;
  // Empty content.text hides the HUD. Recording animation is independent of replies.
  virtual void ShowIndicator(GLFWwindow* window, const Indicator::Content& content) = 0;
  // Microphone and speaker ids from ListAudioDevices; empty means the system
  // default. Streams opened afterwards use them, and a device that is no
  // longer connected falls back to the default.
  virtual void SetDevices(const std::string& input, const std::string& output) = 0;
};

struct AudioDevice {
  std::string id, name;
};
struct AudioDevices {
  std::vector<AudioDevice> inputs, outputs;
};
// Connected microphones and speakers. Ids (Windows endpoint ids, Core Audio
// device UIDs) stay the same across restarts. Callable from any thread.
AudioDevices ListAudioDevices();

std::unique_ptr<Platform> MakePlatform();
// Store secrets in the current user's Keychain / Windows Credential Manager.
// The data directory identifies the profile, including isolated test profiles.
// Deletes the Bailian key saved by versions that used it for search and vision.
void RemoveLegacyApiKey(const std::string& profile);
// API key of a chat service in ChatProviders(). "thirdparty" keeps the key of
// the former custom endpoint setting.
bool SaveChatApiKey(const std::string& profile, const std::string& provider, const std::string& key, std::string& error);
std::string LoadChatApiKey(const std::string& profile, const std::string& provider, std::string& error);
}  // namespace Voice
