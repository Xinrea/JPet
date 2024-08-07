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

 public:
  ~DataManager() { Save(); };

  void GetWindowPos(int* x, int* y);
  void UpdateWindowPos(int x, int y);

  void GetAudio(int* volume, bool* mute);
  void UpdateAudio(int volume, bool mute);

  void GetShortcut(std::map<std::string, std::string>* shortcuts);
  void UpdateShortcut(const std::string& key, const std::string& value);

  void GetDisplay(float* scale, bool* green, bool* rateLimit);
  void UpdateDisplay(float scale, bool green, bool rateLimit);

  void GetNotify(std::vector<std::string>* followList, bool* dynamic,
                 bool* live, bool* update);
  void UpdateNotify(const std::vector<std::string>& followList, bool dynamic,
                    bool live, bool update);

  void SetRaw(const std::string& key, int value);
  void SetRaw(const std::string& key, float value);
  int GetRaw(const std::string& key);
  int GetWithDefault(const std::string& key, int default_value);
  float GetWithDefault(const std::string& key, float default_value);

  void AddExp(bool bonus);

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

  void Save();

  static DataManager* GetInstance();
};
