#pragma once

#include "UpdateRelease.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

// One worker owns all network and extraction work; API reads only cached state.
class UpdateManager {
 public:
  static UpdateManager* GetInstance();
  ~UpdateManager();
  void Start(std::function<void(const Updates::Release&)> notify = {});
  void Stop();
  nlohmann::json Status();
  bool Check(std::string& error);
  bool Download(std::string& error);
  bool Install(std::string& error);
  // Called on the application thread. Exit only after the helper starts.
  bool ApplyPendingInstall();

 private:
  UpdateManager() = default;
  void Run();
  void CheckRelease();
  void DownloadRelease();
  bool Busy() const;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::thread worker_;
  std::atomic_bool stopping_{true};
  bool checkRequested_ = false, downloadRequested_ = false, installRequested_ = false;
  std::string state_ = "idle", error_, installError_, notifiedVersion_;
  Updates::Release release_;
  uint64_t downloaded_ = 0;
  std::filesystem::path work_, staged_;
  std::function<void(const Updates::Release&)> notify_;
};
