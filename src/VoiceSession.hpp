#pragma once

#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <set>
#include <vector>

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

struct ToolCall {
  std::string id, name, arguments;
};

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
    std::function<void(const std::vector<ToolCall>&)> tools;
  };
  explicit Session(Callbacks callbacks, nlohmann::json tools = nlohmann::json::array());
  void Reset();
  void BeginInput();
  void AppendInput(const std::string& pcm);
  void EndInput();
  void Receive(const nlohmann::json& event);
  void CompleteTool(const std::string& callId, const nlohmann::json& result);
  bool Ready() const { return ready_; }
  bool Recording() const { return inputOpen_; }
  bool Busy() const { return inputOpen_ || submitPending_ || responseActive_ || !pendingTools_.empty(); }

 private:
  void Send(nlohmann::json event);
  void FlushInput();
  void Submit();
  void Status(const std::string& state, const std::string& message = "");
  void Interrupt();
  void CancelResponse();
  void CollectTool(const nlohmann::json& item);
  void ContinueResponse();
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
  nlohmann::json tools_;
  std::vector<ToolCall> toolCalls_;
  std::set<std::string> seenTools_, pendingTools_;
  std::set<std::string> retiredResponses_;
  size_t toolCount_ = 0;
};
}  // namespace Voice
