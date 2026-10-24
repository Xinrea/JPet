#pragma once

#include <string>
#include <toml++/toml.hpp>
#include <vector>
#include <mutex>
#include <cstdint>
#include <nlohmann/json.hpp>

#include "GameData.hpp"

class DataManager {
 private:
  toml::table data;
  std::shared_ptr<GameData> gameData;
  std::recursive_mutex gameMutex;
  bool init();
  DataManager();

  void initNotifySection();
  // The [voice] table, created when missing; callers hold gameMutex.
  toml::table& VoiceTable();

 public:
  ~DataManager() { Save(); };

  template <typename T> T GetConfig(const std::string &section, const std::string &key, T dvalue) {
    std::lock_guard<std::recursive_mutex> lock(gameMutex);
    T ret;
    try {
      ret = data.at(section).as_table()->at(key).value_or(dvalue);
      return ret;
    } catch(const std::exception& e) {
      return dvalue;
    }
  }
  
  void GetWindowPos(int *x, int *y);
  void UpdateWindowPos(int x, int y);

  void GetAudio(int* volume, bool* mute, bool* idle_audio, bool* touch_audio);
  void UpdateAudio(int volume, bool mute, bool idle_audio, bool touch_audio);

  nlohmann::json GetVoiceSettings();
  nlohmann::json LoadVoiceHistory();
  void SaveVoiceHistory(const nlohmann::json& history);
  // Applies the optional fields of a settings payload, including write-only
  // API keys, after validating all of them.
  bool UpdateVoiceSettings(const nlohmann::json& payload, std::string& error);

  void GetDisplay(float* scale, bool* green, bool* rateLimit);
  void UpdateDisplay(float scale, bool green, bool rateLimit);

  void GetNotify(bool *dynamic, bool *live, bool *update);
  void UpdateNotify(bool dynamic, bool live, bool update);

  bool GetDropFile();
  void UpdateDropFile(bool b);

  bool IsTracking();
  void IsTracking(bool enable);
  
  std::vector<std::string> GetFollowList();
  void RemoveFollow(const std::string &uid);
  void AddFollow(const std::string &uid);

  template <typename T>
  void SetRaw(const std::string& key, T value) {
    gameData->Update(key, value);
  }

  int GetWithDefault(const std::string& key, int default_value);
  std::string GetWithDefault(const std::string& key, const std::string& default_value);
  float GetWithDefault(const std::string& key, float default_value);

  /**
   * @brief   Status of a task in the pre-cloud save, read once to import it.
   * @return  [0]start_time, [1]end_time, [2]success, [3]status, [4]cost_snapshot
   */
  std::vector<int> TaskStatus(int id);
  // Serializes settings reads and writes shared with the voice tools.
  std::recursive_mutex& GameMutex() { return gameMutex; }

  void Save();

  static DataManager* GetInstance();
};
