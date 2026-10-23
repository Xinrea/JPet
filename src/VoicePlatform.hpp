#pragma once

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
