#pragma once

#include "VoiceCommon.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

namespace Voice {
// Incremental parser for OpenAI-compatible streaming chat completions (SSE):
// text deltas, tool calls assembled from their indexed fragments, the finish
// reason and in-stream errors.
class ChatStreamParser {
 public:
  // Feeds raw response bytes; returns the text delta of the complete events.
  std::string Feed(const std::string& bytes);
  // Call once the HTTP request ends; parses a non-SSE error body if any.
  void Finish(long status);
  bool Done() const { return done_; }
  const std::string& Error() const { return error_; }
  const std::string& FinishReason() const { return finish_; }
  std::vector<ToolCall> ToolCalls() const;

 private:
  void Event(const std::string& data, std::string& text);
  std::string pending_, raw_, error_, finish_;
  struct PartialCall { std::string id, name, arguments; };
  std::vector<PartialCall> calls_;
  bool done_ = false, sse_ = false;
};

struct ChatEvent {
  enum class Type { Text, Done, Failed } type;
  std::string text;               // Text delta, or the error for Failed.
  std::vector<ToolCall> tools;    // Done: complete tool calls.
  std::string finish;             // Done: finish reason.
};

// Streaming chat backend; tests substitute a scripted one.
class ChatBackend {
 public:
  virtual ~ChatBackend() = default;
  virtual void Start(const std::string& url, const std::string& apiKey, const nlohmann::json& body) = 0;
  virtual void Cancel() = 0;
  virtual std::vector<ChatEvent> Poll() = 0;
};

// One streaming request at a time on a worker thread; Start() supersedes the
// previous request, whose remaining events are dropped.
class ChatClient final : public ChatBackend {
 public:
  using Event = ChatEvent;
  ChatClient() = default;
  ~ChatClient() override;
  void Start(const std::string& url, const std::string& apiKey, const nlohmann::json& body) override;
  void Cancel() override;
  bool Active() const { return active_; }
  std::vector<Event> Poll() override;

 private:
  struct Shared {
    std::mutex mutex;
    std::vector<std::pair<uint64_t, Event>> events;
    std::atomic<uint64_t> generation{0};
  };
  std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
  std::vector<std::thread> workers_;
  bool active_ = false;
};

// Bailian's OpenAI-compatible endpoint in a Beijing workspace, or a custom base URL.
std::string ChatCompletionsUrl(const std::string& baseUrl, const std::string& workspace);
// HTTPS, or plain HTTP to this machine, with no credentials or fragments.
bool ValidChatBaseUrl(const std::string& url);
}  // namespace Voice
