#pragma once

#include "VoiceCascade.hpp"
#include "VoiceModels.hpp"
#include "VoiceSpeech.hpp"
#include "VoicePlatform.hpp"
#include "VoiceCommon.hpp"
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
  bool ClearHistory() {
    historyClearRequested_ = resetRequested_ = true;
    return history_.Clear();
  }
  bool IsBusy() const;
  // A typed message from the panel's chat; answered in text by the next Tick.
  bool SendText(const std::string& text, std::string& error);
  void StopReply() { stopRequested_ = true; }
  // Local speech recognition and synthesis models, managed as one download:
  // download, cancel or remove them.
  bool SpeechModelAction(const std::string& action, std::string& error);
  nlohmann::json SpeechModelStatus();
  // Models of a chat service for the settings panel ({provider, base_url?,
  // api_key?}; the saved key when none is typed): {models: [{id, name}]}.
  nlohmann::json ChatModels(const nlohmann::json& request, std::string& error);

 private:
  using Clock = std::chrono::steady_clock;
  VoiceChat();
  // Starts the pipeline: with voice for the shortcut, text-only for typed
  // messages. A text-only pipeline is replaced when the microphone opens.
  bool Connect(bool voice);
  void Begin();
  void End();
  void Drain();
  void Close(bool failed = false);
  void Fail(const std::string& error);
  void SetState(const std::string& state, const std::string& message = "");
  bool Recording() const { return pipeline_ && pipeline_->Recording(); }
  bool StartPipeline(Voice::CascadeConfig config, nlohmann::json tools, bool voice);
  void PlayReply(const std::string& pcm);
  void SubmitTools(const std::vector<Voice::ToolCall>& calls);
  std::vector<Voice::SpeechModelStore*> ModelStores();  // Recognition, synthesis.
  std::unique_ptr<Voice::Platform> platform_;
  std::unique_ptr<Voice::ToolExecutor> tools_;
  Voice::History history_;
  std::unique_ptr<Voice::Pipeline> pipeline_;
  // Local turn-detection models, loaded once and shared by successive pipelines.
  std::unique_ptr<Voice::SileroVad> vad_;
  std::unique_ptr<Voice::SmartTurn> smartTurn_;
  std::string turnModelError_;
  std::once_flag modelStoreOnce_;
  std::unique_ptr<Voice::SpeechModelStore> recognitionModel_, synthesisModel_;
  // Loaded on the pipeline's recognition worker on first use (about a second).
  std::mutex recognizerMutex_;
  std::unique_ptr<Voice::LocalRecognizer> localRecognizer_;
  std::string recognizerError_;
  std::atomic<bool> resetRequested_{false};
  std::atomic<bool> historyClearRequested_{false};
  std::atomic<bool> busy_{false};
  std::atomic<bool> stopRequested_{false};
  std::mutex textMutex_;
  std::vector<std::string> texts_;
  // Voice and text share one conversation; kept when an idle pipeline closes,
  // reset with the settings or history.
  nlohmann::json context_ = nlohmann::json::array();
  std::mutex statusMutex_;
  nlohmann::json status_ = {{"state", "idle"}, {"message", ""}, {"reply", ""}, {"microphone_on", false}, {"replying", false}};
  GLFWwindow* window_ = nullptr;
  bool connected_ = false, voice_ = false;
  Voice::ShortcutControl shortcut_;
  bool microphoneEnabled_ = false;
  size_t capturedBytes_ = 0;
  double capturedEnergy_ = 0;
  int capturedPeak_ = 0;
  std::string indicator_;
  bool indicatorError_ = false;
  Clock::time_point lastActivity_, stateChangedAt_;
};
