#pragma once
#include <nlohmann/json.hpp>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <string>

// OAuth credentials stay in the native process; the settings panel sees status only.
class PowerLiveAccount {
 public:
  static PowerLiveAccount& Instance();
  ~PowerLiveAccount();
  void Configure(const std::string& profile, const std::string& service, int port);
  nlohmann::json Status();
  bool Login(std::string& error);
  bool Callback(const std::string& state, const std::string& code, const std::string& failure, std::string& error);
  bool Logout(std::string& error);
  std::string CachedAccessToken(std::string& error);
  std::string AccessToken(std::string& error);
 private:
  PowerLiveAccount();
  void Run();
  void Wake();
  bool Refresh(std::string& error);
  bool Install(const nlohmann::json& tokens, std::string& error);
  std::mutex mutex_, operation_;
  std::condition_variable wake_;
  std::thread worker_;
  bool stopping_ = false, requested_ = false, configured_ = false, refreshing_ = false;
  std::string profile_, service_, redirect_, access_, idToken_, state_, verifier_, error_;
  int64_t expires_ = 0, loginExpires_ = 0, quotaAt_ = 0, attemptAt_ = 0;
  nlohmann::json user_ = nullptr, quota_ = nullptr;
  bool serviceReady_ = false;
};
