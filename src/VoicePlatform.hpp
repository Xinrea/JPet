#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace Voice {
// WebSocket channels. None is opened today: speech synthesis runs on this machine.
constexpr int SynthesisChannel = 0, Channels = 1;
constexpr int LocalChannel = -1;  // Microphone, playback and other local events.

struct Event {
  enum class Type { Message, Binary, Microphone, Error, Diagnostic };
  Type type;
  std::string data;
  int channel = LocalChannel;
};

// All methods are called on the application thread. Native callbacks only
// enqueue events, so neither audio nor network callbacks touch the renderer.
class Platform {
 public:
  virtual ~Platform() = default;
  virtual bool ShortcutHeld() const = 0;
  // Opens a WebSocket on `channel`, replacing any previous connection there.
  virtual void Connect(int channel, const std::string& url, const std::string& apiKey) = 0;
  virtual void Disconnect(int channel) = 0;
  void DisconnectAll() { for (int channel = 0; channel < Channels; ++channel) Disconnect(channel); }
  virtual void Send(int channel, const std::string& message) = 0;
  virtual void StartCapture() = 0;
  virtual void StopCapture() = 0;
  virtual std::vector<Event> Poll() = 0;
  virtual void Play(const std::string& pcm, float volume) = 0;
  virtual bool IsPlaying() const = 0;
  virtual void StopPlayback() = 0;
  // 24 kHz reply samples heard since the last StopPlayback (approximate on
  // devices that buffer ahead).
  virtual uint64_t PlayedSamples() const = 0;
  virtual void ShowIndicator(GLFWwindow* window, const std::string& text,
                             bool error) = 0;
};

std::unique_ptr<Platform> MakePlatform();
// Store secrets in the current user's Keychain / Windows Credential Manager.
// The data directory identifies the profile, including isolated test profiles.
bool SaveApiKey(const std::string& profile, const std::string& key,
                std::string& error);
std::string LoadApiKey(const std::string& profile, std::string& error);
// API key for a custom OpenAI-compatible chat endpoint (cascade engine).
bool SaveChatApiKey(const std::string& profile, const std::string& key, std::string& error);
std::string LoadChatApiKey(const std::string& profile, std::string& error);
}  // namespace Voice
