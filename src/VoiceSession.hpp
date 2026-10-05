#pragma once

#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

namespace Voice {
constexpr const char* Model = "qwen3.8-omni-flash-realtime";
constexpr size_t InputBytesPerSecond = 16000 * 2;

std::string Trim(const std::string& value);
bool ValidWorkspace(const std::string& value);
bool ValidApiKey(const std::string& value);
std::string ConnectionUrl(const std::string& workspace);
std::string EncodeBase64(const std::string& bytes);
std::string DecodeBase64(const std::string& encoded);
std::string FriendlyError(const std::string& code);

// Push-to-talk protocol, independent of devices and credentials. The same
// state machine is used by both native backends and deterministic tests.
class Session {
 public:
  struct Callbacks {
    std::function<void(const nlohmann::json&)> send;
    std::function<void(const std::string&)> play;
    std::function<bool()> isPlaying;
    std::function<void()> stopPlayback;
    std::function<void(const std::string&, const std::string&)> status;
    std::function<void(const std::string&)> transcript;
  };
  explicit Session(Callbacks callbacks);
  void Reset();
  void BeginInput();
  void AppendInput(const std::string& pcm);
  void EndInput();
  void Receive(const nlohmann::json& event);
  bool Ready() const { return ready_; }
  bool Recording() const { return inputOpen_; }
  bool Busy() const { return inputOpen_ || submitPending_ || responseActive_; }

 private:
  void Send(nlohmann::json event);
  void FlushInput();
  void Submit();
  void Status(const std::string& state, const std::string& message = "");
  void Interrupt();
  void CancelResponse();
  Callbacks callbacks_;
  uint64_t eventId_ = 0;
  bool ready_ = false;
  bool inputOpen_ = false;
  bool submitPending_ = false;
  bool responseActive_ = false;
  bool acceptReply_ = false;
  bool cancelRequested_ = false;
  size_t inputBytes_ = 0;
  std::string bufferedInput_;
  std::string responseId_;
  std::string reply_;
};
}  // namespace Voice
