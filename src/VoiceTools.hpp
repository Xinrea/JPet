#pragma once

#include "VoiceSession.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace Voice {
nlohmann::json ToolDefinitions();
std::string ToolLabel(const std::string& name);

// Credentials and platform APIs live exclusively in the runtime adapters.
struct ToolDependencies {
  std::function<nlohmann::json(const nlohmann::json&)> game;
  std::function<std::string(const nlohmann::json&)> command;
  std::function<nlohmann::json(const nlohmann::json&)> desktop, web, bilibili;
  std::function<bool(const std::string&, std::string&)> openUrl;
};
nlohmann::json ExecuteTool(const ToolCall& call, const ToolDependencies& dependencies);
nlohmann::json GameView(const nlohmann::json& snapshot, const nlohmann::json& profile,
    const nlohmann::json& tasks, const nlohmann::json& achievements,
    const nlohmann::json& connection, const nlohmann::json& identity, const std::string& section);
nlohmann::json BilibiliSearchResults(const nlohmann::json& response,
    const std::string& kind, int limit);
nlohmann::json WebSearchResults(const nlohmann::json& response, int limit);

class ToolExecutor {
 public:
  explicit ToolExecutor(ToolDependencies dependencies);
  ~ToolExecutor();
  void Submit(const std::vector<ToolCall>& calls);
  void Cancel();
  struct Result { ToolCall call; nlohmann::json value; };
  std::vector<Result> Poll();
 private:
  struct Job { ToolCall call; uint64_t generation; };
  void Run();
  ToolDependencies dependencies_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<Job> jobs_;
  std::vector<Result> results_;
  uint64_t generation_ = 0;
  bool stopping_ = false;
  std::thread worker_;
};
ToolDependencies MakeToolDependencies();
} // namespace Voice
