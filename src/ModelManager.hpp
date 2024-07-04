#pragma once
#include <string>

class ModelManager {
  ModelManager();

 public:
  std::string modelName;
  std::string modelFileName;
  static ModelManager* GetInstance();
};