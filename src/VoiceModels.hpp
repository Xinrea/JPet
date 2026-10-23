#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

namespace Voice {
// Downloads a local voice model (speech recognition or synthesis) on demand
// into the profile directory, verifying each file's size and SHA-256 before it
// is used.
class SpeechModelStore {
 public:
  struct File { const char* name; uint64_t size; const char* sha256; };
  // Mirrors are tried in order; each is a URL prefix for the file names.
  SpeechModelStore(std::string directory, std::vector<std::string> mirrors, std::vector<File> files = Files());
  ~SpeechModelStore();
  // Directory holding the verified files, or empty when not installed.
  std::string Installed() const;
  void Download();
  void Cancel();
  bool Remove(std::string& error);
  // {state: missing|downloading|ready|failed, received, total, error}
  nlohmann::json Status() const;

  // SenseVoice-Small for speech recognition.
  static const std::vector<File>& Files();
  static std::vector<std::string> DefaultMirrors();
  // Qwen3-TTS 0.6B Base (talker, then codec) for speech synthesis.
  static const std::vector<File>& SynthesisFiles();
  static std::vector<std::string> SynthesisMirrors();
  // One status for several stores that the settings present as one download.
  static nlohmann::json Combine(const std::vector<nlohmann::json>& statuses);

 private:
  void Run(uint64_t generation);
  std::string directory_;
  std::vector<std::string> mirrors_;
  std::vector<File> files_;
  mutable std::mutex mutex_;
  std::string state_ = "missing", error_;
  std::atomic<uint64_t> received_{0}, generation_{0};
  std::thread worker_;
};
}  // namespace Voice
