#pragma once

#include <map>
#include <string>
#include <toml++/toml.hpp>
#include <vector>

#include "GameData.hpp"

class DataManager {
 private:
  toml::table data;
  std::shared_ptr<GameData> gameData;
  bool init();
  DataManager();

 public:
  ~DataManager() = default;

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

  void GetModalState(std::map<std::string, float>* modalState);
  void UpdateModalState(const std::map<std::string, float>& modalState);

  void AddExp(bool bonus);
  int GetExp();

  /**
   * @brief   Get the list of attributes.
   * @return The list of attributes.
   * [0]speed,[1]endurance,[2]strength,[3]will,[4]intellect
   */
  std::vector<int> GetAttributeList();

  void Save();

  static DataManager* GetInstance();
};
