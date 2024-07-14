#include "ModelManager.hpp"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

#include "LAppPal.hpp"

namespace {
ModelManager* s_instance = NULL;
}

ModelManager* ModelManager::GetInstance() {
  if (s_instance == NULL) {
    s_instance = new ModelManager();
  }
  return s_instance;
}

ModelManager::ModelManager() {
  // load model config from resources/model_config.json
  std::ifstream f("resources/model_config.json");
  nlohmann::json doc = nlohmann::json::parse(f);
  if (!doc.contains("name")) {
    LAppPal::PrintLog("[ModelManager]model_config.json does not have name");
    modelName = "default";
  } else {
    modelName = doc["name"];
  }
  if (!doc.contains("model_path")) {
    std::cerr << "model_config.json does not have model_path" << std::endl;
    // should fatal here
    throw std::runtime_error("model_config.json does not have model_path");
    return;
  }
  modelFileName = doc["model_path"];
  LAppPal::PrintLog("[ModelManager]Model name: %s", modelName.c_str());
  LAppPal::PrintLog("[ModelManager]Model path: %s", modelFileName.c_str());
}