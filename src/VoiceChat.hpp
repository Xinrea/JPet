#pragma once

#include "VoicePlatform.hpp"
#include "VoiceSession.hpp"
#include "VoiceHistory.hpp"
#include "VoiceTools.hpp"
#include "VoiceShortcut.hpp"
#include <atomic>
#include <chrono>
#include <mutex>

class VoiceChat {
 public:
  static VoiceChat* GetInstance();
  void Tick(GLFWwindow* window);
  void Stop();
  void ConfigurationChanged() { resetRequested_ = true; }
  nlohmann::json Status();
  nlohmann::json History() { return history_.Snapshot(); }
  bool IsBusy() const;

 private:
  using Clock = std::chrono::steady_clock;
  VoiceChat();
  void Begin();
  void End();
  void Drain();
  void Close(bool failed = false);
  void Fail(const std::string& error);
  void SetState(const std::string& state, const std::string& message = "");
  std::unique_ptr<Voice::Platform> platform_;
  std::unique_ptr<Voice::ToolExecutor> tools_;
  Voice::History history_;
  Voice::Session session_;
  std::atomic<bool> resetRequested_{false};
  std::atomic<bool> busy_{false};
  std::mutex statusMutex_;
  nlohmann::json status_ = {{"state", "idle"}, {"message", ""}, {"reply", ""}, {"microphone_on", false}};
  GLFWwindow* window_ = nullptr;
  bool connected_ = false;
  Voice::ShortcutControl shortcut_;
  bool microphoneEnabled_ = false;
  bool failurePending_ = false;
  size_t capturedBytes_ = 0;
  size_t sentBytes_ = 0;
  size_t sentChunks_ = 0;
  double capturedEnergy_ = 0;
  int capturedPeak_ = 0;
  std::string error_;
  std::string indicator_;
  bool indicatorError_ = false;
  Clock::time_point connectedAt_, lastActivity_, stateChangedAt_;
};
