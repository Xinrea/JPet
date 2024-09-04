#pragma once

#include <map>
#include <string>
#include <toml++/toml.hpp>
#include <vector>

#include "GameData.hpp"
#include "GameTask.hpp"

class DataManager {
 private:
  toml::table data;
  std::shared_ptr<GameData> gameData;
  std::vector<std::shared_ptr<GameTask>> tasks;
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

  int GetAttribute(const std::string& key);

  void AddAttribute(const std::string& key, int value);

  void DumpTask(int id, int start_time, int end_time, int success, int status,
                int cost_snapshot);

  /**
   * @brief   Get task status.
   * @return  status list. [0]start_time, [1]end_time, [2]success, [3]status
   */
  std::vector<int> TaskStatus(int id);
  std::vector<std::shared_ptr<GameTask>> GetTasks() {
    if (tasks.empty()) {
      tasks = GameTask::InitTasks();
    }
    return tasks;
  }
  std::shared_ptr<GameTask> GetCurrentTask() {
    for (auto& task : tasks) {
      if (task->status == TStatus::RUNNING || task->status == TStatus::WAIT_SETTLE) {
        return task;
      }
    }
    return nullptr;
  }

  void Save();

  void SetResetMark();

  bool IsResetMarked();

  static DataManager* GetInstance();
};
