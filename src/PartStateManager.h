#pragma once
#include <CubismFramework.hpp>
#include <map>
#include <string>
#include <vector>
#include <Model/CubismModel.hpp>
#include <Utils/CubismJson.hpp>
#include <Motion/CubismExpressionMotion.hpp>

using std::map;
using std::string;
using std::vector;
using namespace Live2D::Cubism::Framework;

struct ParamEntry {
  string key;
  CubismIdHandle id;
  float value;
  float getValue(CubismModel *model) const { return model->GetParameterValue(id); }
};

class PartStateManager {
  CubismModel *model = nullptr;

 public:
  map<string, ParamEntry> param_map_ = {
      {"ParamMouth1", {"ParamMouth1", nullptr, 30}},
      {"ParamMouth2", {"ParamMouth2", nullptr, 0}},
      {"ParamMouth3", {"ParamMouth3", nullptr, 0}},
      {"ParamMouth4", {"ParamMouth4", nullptr, 0}},
      {"ParamMouth5", {"ParamMouth5", nullptr, 0}},
      {"ParamMouth6", {"ParamMouth6", nullptr, 0}},
      {"ParamEyeStar", {"ParamEyeStar", nullptr, 0}},
      {"ParamHair", {"ParamHair", nullptr, 0}},
      {"ParamCloth1", {"ParamCloth1", nullptr, 30}},
      {"ParamCloth2", {"ParamCloth2", nullptr, 0}},
      {"ParamCloth3", {"ParamCloth3", nullptr, 0}},
      {"ParamBlackFace", {"ParamBlackFace", nullptr, 0}},
      {"ParamRedFace", {"ParamRedFace", nullptr, 0}},
      {"ParamSweat", {"ParamSweat", nullptr, 0}},
      {"ParamDizzy", {"ParamDizzy", nullptr, 0}},
      {"ParamREars", {"ParamREars", nullptr, 0}},
      {"ParamLEars", {"ParamLEars", nullptr, 0}},
      {"ParamHat", {"ParamHat", nullptr, 30}},
      {"ParamGun", {"ParamGun", nullptr, 0}},
      {"ParamGlasses", {"ParamGlasses", nullptr, 0}},
      {"ParamShoes", {"ParamShoes", nullptr, 30}},
      {"ParamLegs", {"ParamLegs", nullptr, 30}},
      {"ParamTail", {"ParamTail", nullptr, 0}}};

  const std::string prefix_ = "part.";
  static PartStateManager *GetInstance();

  PartStateManager();
  void BindModel(CubismModel *m);
  void SnapshotState();
  void ApplyState();
  map<string, bool> GetStatus();
  void Toggle(const string &key, bool enable);
};
