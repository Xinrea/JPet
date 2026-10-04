#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <nlohmann/json.hpp>

// Only this transport writes cloud snapshots. All gameplay mutations are commands.
class CloudGame {
 public:
  static CloudGame* GetInstance();
  static std::string ServiceUrl();
  void Start();
  void Stop();
  void Wake(bool takeOver = false);
  void Disconnect();
  std::string Command(const nlohmann::json& action);
  void Touch();
  nlohmann::json Status();
  bool Online();
  ~CloudGame();

 private:
  CloudGame() = default;
  std::mutex networkMutex_, statusMutex_, waitMutex_;
  std::condition_variable wake_;
  std::thread worker_;
  std::atomic<bool> running_{false}, requested_{false}, takeOver_{false};
  std::atomic<int> touches_{0};
  std::string uid_, name_, session_, url_;
  bool opened_ = false, ready_ = false;
  std::string error_;
  nlohmann::json pending_;
  void Run();
  void Sync();
  void Close();
  nlohmann::json Payload();
  std::string Request(const std::string& kind, const nlohmann::json& payload, bool* received = nullptr);
  void SetStatus(bool ready, const std::string& error);
  bool ReplayPending();
};
