#pragma once

#include "VoicePlatform.hpp"
#include "VoiceSession.hpp"
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
  bool IsBusy() const;

 private:
  using Clock = std::chrono::steady_clock;
  VoiceChat();
  void Begin();
  void Drain();
  void Close();
  void Fail(const std::string& error);
  void SetState(const std::string& state, const std::string& message = "");
  std::unique_ptr<Voice::Platform> platform_;
  Voice::Session session_;
  std::atomic<bool> resetRequested_{false};
  std::atomic<bool> busy_{false};
  std::mutex statusMutex_;
  nlohmann::json status_ = {{"state", "idle"}, {"message", ""}, {"reply", ""}};
  GLFWwindow* window_ = nullptr;
  bool connected_ = false;
  bool rawHeld_ = false;
  bool turnStarted_ = false;
  bool waitForRelease_ = false;
  bool failurePending_ = false;
  std::string error_;
  std::string indicator_;
  bool indicatorError_ = false;
  Clock::time_point keyPressedAt_, connectedAt_, lastActivity_, stateChangedAt_;
};
