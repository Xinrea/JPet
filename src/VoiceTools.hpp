#pragma once

#include "VoiceCommon.hpp"
#include "StateMessage.hpp"
#include <map>
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
  // desktop returns the screenshot as base64 JPEG in ToolImageField.
  std::function<nlohmann::json(const nlohmann::json&)> desktop, bilibili;
  std::function<bool(const std::string&, std::string&)> openUrl;
  std::function<nlohmann::json(const nlohmann::json&, const std::function<bool()>&)> settings;
};
nlohmann::json ExecuteTool(const ToolCall& call, const ToolDependencies& dependencies,
    const std::function<bool()>& cancelled = {});
// `game` is a GameManager snapshot whose profile may carry the medal level.
nlohmann::json GameView(const nlohmann::json& game, const std::string& section, size_t offset = 0, size_t limit = 5);
// Preserve the configured list even when a watcher's cached name is not available yet.
nlohmann::json WatchListView(const std::vector<std::string>& uids, const std::map<std::string, WatchTarget>& targets);

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
// Pump settings on the render thread; cancellation/shutdown releases waiting workers.
void DrainSettingsTools();
void CancelSettingsTools();
void StopSettingsTools();
} // namespace Voice
