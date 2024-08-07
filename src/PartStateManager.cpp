#include "PartStateManager.h"
#include "DataManager.hpp"
#include "LAppLive2DManager.hpp"
#include "LAppModel.hpp"

#include <Id/CubismIdManager.hpp>
#include <Motion/CubismExpressionMotion.hpp>

namespace {
PartStateManager* s_instance = NULL;
}

PartStateManager::PartStateManager() {}

PartStateManager* PartStateManager::GetInstance() {
  if (s_instance == NULL) {
    s_instance = new PartStateManager();
  }
  return s_instance;
}

void PartStateManager::BindModel(CubismModel* m) {
  model = m;
  for (auto& [_, entry] : param_map_) {
    entry.id = CubismFramework::GetIdManager()->GetId(entry.key.c_str());
  }
}

void PartStateManager::SnapshotState() {
  if (model == nullptr) {
    return;
  }
  for (auto& [_, entry] : param_map_) {
    DataManager::GetInstance()->SetRaw(prefix_ + entry.key,
                                       model->GetParameterValue(entry.id));
  }
  LAppPal::PrintLog(LogLevel::Debug, "[PartStateManager]All state snapshot");
}

void PartStateManager::ApplyState() {
  if (model == nullptr) return;
  for (auto& [_, entry] : param_map_) {
    model->SetParameterValue(entry.id,
                             DataManager::GetInstance()->GetWithDefault(
                                 prefix_ + entry.key, entry.value));
  }
  LAppPal::PrintLog(LogLevel::Info,
                    "[PartStateManager]Appy stored state to model");
}

void PartStateManager::Toggle(const std::string& key, bool enable) {
  if (model == nullptr) {
    return;
  }
  LAppModel* current = LAppLive2DManager::GetInstance()->GetModel(0);
  auto it = param_map_.find(key);
  if (it == param_map_.end()) {
    return;
  }
  auto& entry = it->second;
  if (enable && entry.getValue(model) > 0) {
    return;
  }
  if (!enable && entry.getValue(model) == 0) {
    return;
  }
  // construct expresion
  if (LAppPal::StartWith(entry.key, "ParamCloth")) {
    // create special expression that makes this key to 30 but others to 0
    csmVector<CubismExpressionMotion::ExpressionParameter> parameters;
    for (const string& ckey : clothes_keys_) {
      if (ckey == entry.key) {
        if (entry.getValue(model) == 30) {
          continue;
        }
        CubismExpressionMotion::ExpressionParameter param;
        param.ParameterId = param_map_[ckey].id;
        param.BlendType = CubismExpressionMotion::ExpressionBlendType::Additive;
        param.Value = 30;
        parameters.PushBack(param);
        continue;
      }
      CubismExpressionMotion::ExpressionParameter param;
      param.ParameterId = param_map_[ckey].id;
      param.BlendType = CubismExpressionMotion::ExpressionBlendType::Additive;
      param.Value = -30;
      parameters.PushBack(param);
    }

    auto motion = CubismExpressionMotion::Create(parameters, 0.5f, 0.5f);
    current->StartMotion(motion);
    return;
  }
  if (LAppPal::StartWith(entry.key, "ParamMouth")) {
    csmVector<CubismExpressionMotion::ExpressionParameter> parameters;
    for (const string& mkey : mouth_keys_) {
      if (mkey == entry.key) {
        if (entry.getValue(model) == 30) {
          continue;
        }
        CubismExpressionMotion::ExpressionParameter param;
        param.ParameterId = param_map_[mkey].id;
        param.BlendType = CubismExpressionMotion::ExpressionBlendType::Additive;
        param.Value = 30;
        parameters.PushBack(param);
        continue;
      }
      CubismExpressionMotion::ExpressionParameter param;
      param.ParameterId = param_map_[mkey].id;
      param.BlendType = CubismExpressionMotion::ExpressionBlendType::Additive;
      param.Value = -30;
      parameters.PushBack(param);
    }
    auto motion = CubismExpressionMotion::Create(parameters, 0.5f, 0.5f);
    current->StartMotion(motion);
    return;
  }
  auto motion = CubismExpressionMotion::Create(entry.id, enable ? 30 : -30, 0.5f, 0.5f);
  current->StartMotion(motion);
  return;
}

map<string, bool> PartStateManager::GetStatus() {
  map<string, bool> ret;
  for (const auto& [_, entry] : param_map_) {
    ret[entry.key] = (entry.getValue(model) > 0);
  }
  return ret;
}
