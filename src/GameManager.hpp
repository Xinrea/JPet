#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <nlohmann/json.hpp>

#include "GameRules.hpp"

// Owns the game save and runs it while JPet is open. Every gameplay change
// goes through Command, Touch or the background tick.
class GameManager {
 public:
  static GameManager* GetInstance();
  // Starts crediting time. Time while JPet is closed is never credited.
  void Start();
  void Stop();
  // Returns an empty string on success, otherwise the reason for rejection.
  std::string Command(const nlohmann::json& action);
  void Touch();
  // {profile, tasks, achievements, save}; see GameRules::Snapshot.
  nlohmann::json Snapshot();
  float TaskProgress();
  // The outfit to show after a change, or -1. Called by the render thread,
  // which owns the Live2D model.
  int TakeClothesChange() { return pendingClothes_.exchange(-1); }
  ~GameManager();

 private:
  GameManager() = default;
  std::mutex mutex_, waitMutex_;
  std::condition_variable wake_;
  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<int> pendingClothes_{-1};
  bool loaded_ = false;
  GameRules::State state_;
  // Wall-clock time credited so far; 0 while the game is not running.
  int64_t clock_ = 0;
  int64_t savedAt_ = 0;
  std::mt19937_64 random_{std::random_device{}()};

  void Run();
  void Tick();
  void Load();
  void Advance(int64_t now);
  void Persist(int64_t now);
  // Effects of a state change, applied after the game lock is released.
  struct Changes {
    bool any = false;
    nlohmann::json unlocked = nlohmann::json::array();
    nlohmann::json settled;  // The newest history record when a task settled.
    int clothes = -1;
  };
  // Saves the game and describes what changed since `before` (a Fingerprint).
  Changes Commit(const nlohmann::json& before, int64_t now);
  void Publish(const Changes& changes);
};
