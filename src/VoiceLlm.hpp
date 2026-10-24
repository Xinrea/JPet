#pragma once

#include "VoiceCommon.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
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

// OpenAI-compatible chat services offered in settings. Each keeps its own API key.
struct ChatProvider {
  const char* id;       // Saved in settings.
  const char* name;
  const char* baseUrl;  // Empty: the user supplies an OpenAI-compatible endpoint.
};
const std::vector<ChatProvider>& ChatProviders();
const ChatProvider* FindChatProvider(const std::string& id);
// Request fields that turn reasoning off: spoken replies need to start quickly,
// and reasoning is not carried across tool rounds (DeepSeek rejects tool
// follow-ups that omit it).
nlohmann::json ChatRequestOptions(const std::string& provider);

struct ChatModel {
  std::string id, name;
  std::optional<bool> vision;  // Accepts images; unset when the list does not say.
};
// The provider's GET {base}/models list, sorted by name. OpenRouter lists only
// models that support tools and let reasoning be turned off.
std::vector<ChatModel> ParseChatModels(const std::string& provider, const nlohmann::json& body);
std::vector<ChatModel> FetchChatModels(const std::string& provider, const std::string& baseUrl,
                                       const std::string& apiKey, std::string& error);

// {base}/chat/completions; a base that already ends with it is kept.
std::string ChatCompletionsUrl(const std::string& baseUrl);
// HTTPS, or plain HTTP to this machine, with no credentials or fragments.
bool ValidChatBaseUrl(const std::string& url);
// Any provider's model id, e.g. deepseek-chat or openai/gpt-4o:free.
bool ValidChatModel(const std::string& model);
}  // namespace Voice
