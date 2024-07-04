#include "ModelManager.hpp"

#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

#include <fstream>
#include <iostream>

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
  rapidjson::Document doc;
  std::ifstream ifs("resources/model_config.json");
  if (!ifs.is_open()) {
    LAppPal::PrintLog("[ModelManager]Failed to open model_config.json");
    return;
  }
  rapidjson::IStreamWrapper isw(ifs);
  doc.ParseStream(isw);
  if (doc.HasParseError()) {
    LAppPal::PrintLog("[ModelManager]Failed to parse model_config.json");
    return;
  }
  if (!doc.HasMember("name")) {
    LAppPal::PrintLog("[ModelManager]model_config.json does not have name");
    modelName = "default";
  } else {
    modelName = doc["name"].GetString();
  }
  if (!doc.HasMember("model_path")) {
    std::cerr << "model_config.json does not have model_path" << std::endl;
    // should fatal here
    throw std::runtime_error("model_config.json does not have model_path");
    return;
  }
  modelFileName = doc["model_path"].GetString();
  LAppPal::PrintLog("[ModelManager]Model name: %s", modelName.c_str());
  LAppPal::PrintLog("[ModelManager]Model path: %s", modelFileName.c_str());
}