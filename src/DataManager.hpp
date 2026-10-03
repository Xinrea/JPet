#pragma once

#include <string>
#include <toml++/toml.hpp>
#include <vector>
#include <mutex>
#include <cstdint>

#include "GameData.hpp"
#include "GameTask.hpp"

class DataManager {
 private:
  toml::table data;
  std::shared_ptr<GameData> gameData;
  std::vector<std::shared_ptr<GameTask>> tasks;
  std::recursive_mutex gameMutex;
  nlohmann::json taskQueue = nlohmann::json::array();
  nlohmann::json taskHistory = nlohmann::json::array();
  int64_t nextQueueId = 1;
  void LoadTasks();
  void SaveTaskQueue();
  void StartNextQueuedTask(time_t now);
  bool SettleTask(const std::shared_ptr<GameTask>& task, time_t now);
  std::shared_ptr<GameTask> FindTask(int id);
  bool init();
  DataManager();

  void PostProcess(const std::string& key, int value);

  void initNotifySection();

 public:
  ~DataManager() { Save(); };

  template <typename T> T GetConfig(const string &section, const string &key, T dvalue) {
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

  void SetRaw(const std::string& key, int value) {
    gameData->Update(key, value);
    PostProcess(key, value);
  }

  int GetWithDefault(const std::string& key, int default_value);
  string GetWithDefault(const std::string& key, const string& default_value);
  float GetWithDefault(const std::string& key, float default_value);

  void AddExp();
  int CurrentExpDiff();
  void FetchStar();

  /**
   * @brief   Get the list of attributes.
   * @return The list of attributes.
   * [0]speed,[1]endurance,[2]strength,[3]will,[4]intellect,[5]exp,[6]buycnt
   */
  std::vector<int> GetAttributeList();
  
  int GetAttrLimit();

  int GetAttribute(const std::string& key);

  void AddAttribute(const std::string& key, int value);

  void DumpTask(int id, int start_time, int end_time, int success, int status,
                int cost_snapshot);

  /**
   * @brief   Get task status.
   * @return  status list. [0]start_time, [1]end_time, [2]success, [3]status
   */
  std::vector<int> TaskStatus(int id);
  // Serialize task transitions with API requests and attribute mutations.
  std::recursive_mutex& GameMutex() { return gameMutex; }
  std::shared_ptr<GameTask> GetCurrentTask();
  nlohmann::json GetTaskState();
  int TaskQueueCapacity();
  void TickTasks(time_t now = time(nullptr));
  std::string StartTask(int id, time_t now = time(nullptr));
  std::string QueueTask(int id, time_t now = time(nullptr));
  std::string RemoveQueuedTask(int64_t entryId);
  std::string MoveQueuedTask(int64_t entryId, int direction);
  std::string CancelTask(int id, time_t now = time(nullptr));

  void Save();

  void SetResetMark();

  bool IsResetMarked();

  static DataManager* GetInstance();
};
