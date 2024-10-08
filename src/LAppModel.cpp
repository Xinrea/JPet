﻿/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at
 * https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppModel.hpp"

#include <CubismDefaultParameterId.hpp>
#include <CubismModelSettingJson.hpp>
#include <Id/CubismIdManager.hpp>
#include <Motion/CubismMotionQueueEntry.hpp>
#include <Physics/CubismPhysics.hpp>
#include <Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp>
#include <Utils/CubismString.hpp>

#include "CubismFramework.hpp"
#include "LAppDefine.hpp"
#include "LAppDelegate.hpp"
#include "LAppLive2DManager.hpp"
#include "LAppPal.hpp"
#include "LAppTextureManager.hpp"
#include "PartStateManager.h"
#include "Type/CubismBasicType.hpp"

using namespace Live2D::Cubism::Framework;
using namespace Live2D::Cubism::Framework::DefaultParameterId;
using namespace LAppDefine;

namespace {
csmByte* CreateBuffer(const csmChar* path, csmSizeInt* size) {
  return LAppPal::LoadFileAsBytes(path, size);
}

void DeleteBuffer(csmByte* buffer, const csmChar* path = "") {
  LAppPal::ReleaseBytes(buffer);
}

void FinishedMotion(ACubismMotion* self) {
  PartStateManager::GetInstance()->SnapshotState();
  LAppPal::PrintLog(LogLevel::Debug, "[Model]Motion finished");
}
} // namespace

LAppModel::LAppModel()
    : CubismUserModel(), _modelSetting(NULL), _userTimeSeconds(0.0f) {
  if (DebugLogEnable) {
    _debugMode = true;
  }

  _idParamAngleX = CubismFramework::GetIdManager()->GetId(ParamAngleX);
  _idParamAngleY = CubismFramework::GetIdManager()->GetId(ParamAngleY);
  _idParamAngleZ = CubismFramework::GetIdManager()->GetId(ParamAngleZ);
  _idParamBodyAngleX = CubismFramework::GetIdManager()->GetId(ParamBodyAngleX);
  _idParamEyeBallX = CubismFramework::GetIdManager()->GetId(ParamEyeBallX);
  _idParamEyeBallY = CubismFramework::GetIdManager()->GetId(ParamEyeBallY);
}

LAppModel::~LAppModel() {
  _renderBuffer.DestroyOffscreenSurface();

  ReleaseMotions();
  ReleaseExpressions();

  for (csmInt32 i = 0; i < _modelSetting->GetMotionGroupCount(); i++) {
    const csmChar* group = _modelSetting->GetMotionGroupName(i);
    ReleaseMotionGroup(group);
  }
  delete (_modelSetting);
}

void LAppModel::LoadAssets(const csmChar* dir, const csmChar* fileName) {
  _modelHomeDir = dir;

  if (_debugMode) {
    LAppPal::PrintLog("[APP]load model setting: %s", fileName);
  }

  csmSizeInt size;
  const csmString path = csmString(dir) + fileName;

  csmByte* buffer = CreateBuffer(path.GetRawString(), &size);
  ICubismModelSetting* setting = new CubismModelSettingJson(buffer, size);
  DeleteBuffer(buffer, path.GetRawString());

  SetupModel(setting);

  CreateRenderer();

  SetupTextures();
}

void LAppModel::SetupModel(ICubismModelSetting* setting) {
  // TODO: 模型表情和动作的初始化
  _updating = true;
  _initialized = false;

  _modelSetting = setting;

  csmByte* buffer;
  csmSizeInt size;
  if (DebugLogEnable) {
    LAppPal::PrintLog("[APP]Setup model: %s",
                      _modelSetting->GetModelFileName());
  }
  // Cubism Model
  if (strcmp(_modelSetting->GetModelFileName(), "") != 0) {
    csmString path = _modelSetting->GetModelFileName();
    path = _modelHomeDir + path;

    if (DebugLogEnable) {
      LAppPal::PrintLog("[APP]create model: %s", setting->GetModelFileName());
    }

    buffer = CreateBuffer(path.GetRawString(), &size);
    LoadModel(buffer, size);
    DeleteBuffer(buffer, path.GetRawString());
  }

  // Expression
  if (_modelSetting->GetExpressionCount() > 0) {
    const csmInt32 count = _modelSetting->GetExpressionCount();
    if (_debugMode) {
      LAppPal::PrintLog("[APP]get expression: %d", count);
    }
    for (csmInt32 i = 0; i < count; i++) {
      csmString name = _modelSetting->GetExpressionName(i);
      csmString path = _modelSetting->GetExpressionFileName(i);
      path = _modelHomeDir + path;

      buffer = CreateBuffer(path.GetRawString(), &size);
      ACubismMotion* motion = LoadExpression(buffer, size, name.GetRawString());

      if (_expressions[name] != NULL) {
        ACubismMotion::Delete(_expressions[name]);
        _expressions[name] = NULL;
      }
      _expressions[name] = motion;

      DeleteBuffer(buffer, path.GetRawString());
    }
  }

  SetupPresets();

  // Physics
  if (strcmp(_modelSetting->GetPhysicsFileName(), "") != 0) {
    csmString path = _modelSetting->GetPhysicsFileName();
    path = _modelHomeDir + path;

    buffer = CreateBuffer(path.GetRawString(), &size);
    LoadPhysics(buffer, size);
    DeleteBuffer(buffer, path.GetRawString());
  }

  // Pose
  if (strcmp(_modelSetting->GetPoseFileName(), "") != 0) {
    csmString path = _modelSetting->GetPoseFileName();
    path = _modelHomeDir + path;

    buffer = CreateBuffer(path.GetRawString(), &size);
    LoadPose(buffer, size);
    DeleteBuffer(buffer, path.GetRawString());
  }

  // EyeBlink
  if (_modelSetting->GetEyeBlinkParameterCount() > 0) {
    _eyeBlink = CubismEyeBlink::Create(_modelSetting);
  }

  // Breath
  {
    _breath = CubismBreath::Create();

    csmVector<CubismBreath::BreathParameterData> breathParameters;

    breathParameters.PushBack(CubismBreath::BreathParameterData(
        _idParamAngleX, 0.0f, 15.0f, 6.5345f, 0.5f));
    breathParameters.PushBack(CubismBreath::BreathParameterData(
        _idParamAngleY, 0.0f, 8.0f, 3.5345f, 0.5f));
    breathParameters.PushBack(CubismBreath::BreathParameterData(
        _idParamAngleZ, 0.0f, 10.0f, 5.5345f, 0.5f));
    breathParameters.PushBack(CubismBreath::BreathParameterData(
        _idParamBodyAngleX, 0.0f, 4.0f, 15.5345f, 0.5f));
    breathParameters.PushBack(CubismBreath::BreathParameterData(
        CubismFramework::GetIdManager()->GetId(ParamBreath), 0.5f, 0.5f,
        3.2345f, 0.5f));

    _breath->SetParameters(breathParameters);
  }

  // UserData
  if (strcmp(_modelSetting->GetUserDataFile(), "") != 0) {
    csmString path = _modelSetting->GetUserDataFile();
    path = _modelHomeDir + path;
    buffer = CreateBuffer(path.GetRawString(), &size);
    LoadUserData(buffer, size);
    DeleteBuffer(buffer, path.GetRawString());
  }

  // EyeBlinkIds
  {
    csmInt32 eyeBlinkIdCount = _modelSetting->GetEyeBlinkParameterCount();
    for (csmInt32 i = 0; i < eyeBlinkIdCount; ++i) {
      _eyeBlinkIds.PushBack(_modelSetting->GetEyeBlinkParameterId(i));
    }
  }

  // LipSyncIds
  {
    csmInt32 lipSyncIdCount = _modelSetting->GetLipSyncParameterCount();
    for (csmInt32 i = 0; i < lipSyncIdCount; ++i) {
      _lipSyncIds.PushBack(_modelSetting->GetLipSyncParameterId(i));
    }
  }

  // mouthIds
  {
    for (csmInt32 i = 1; i <= 6; i++) {
      std::string param_key = "ParamMouth" + std::to_string(i);
      _mouthIds.PushBack(CubismFramework::GetIdManager()->GetId(param_key.c_str()));
    }
  }

  // Layout
  csmMap<csmString, csmFloat32> layout;
  _modelSetting->GetLayoutMap(layout);
  _modelMatrix->SetupFromLayout(layout);

  PartStateManager::GetInstance()->BindModel(_model);
  PartStateManager::GetInstance()->ApplyState();

  LAppPal::PrintLog(LogLevel::Info,
                    "[Model]Model motions count: %d, groups: %d",
                    _modelSetting->GetMotionCount("All"),
                    _modelSetting->GetMotionGroupCount());
  for (csmInt32 i = 0; i < _modelSetting->GetMotionGroupCount(); i++) {
    const csmChar* group = _modelSetting->GetMotionGroupName(i);
    PreloadMotionGroup(group);
  }

  _motionManager->StopAllMotions();

  _model->SaveParameters();
  
  _updating = false;
  _initialized = true;
}

void LAppModel::SetupPresets() {
  // drag_on and drag_off
  auto dragExpression =
      CubismExpressionMotion::Create("ParamDrag", 30, 0.5f, 0.5f);
  _presets["drag_on"] = dragExpression;
  dragExpression = CubismExpressionMotion::Create("ParamDrag", 0, 0.5f, 0.5f);
  _presets["drag_off"] = dragExpression;
}

void LAppModel::PreloadMotionGroup(const csmChar* group) {
  const csmInt32 count = _modelSetting->GetMotionCount(group);

  for (csmInt32 i = 0; i < count; i++) {
    // ex) idle_0
    csmString name = Utils::CubismString::GetFormatedString("%s_%d", group, i);
    csmString path = _modelSetting->GetMotionFileName(group, i);
    path = _modelHomeDir + path;

    LAppPal::PrintLog("[APP]load motion: %s => [%s_%d] ", path.GetRawString(),
                      group, i);

    csmByte* buffer;
    csmSizeInt size;
    buffer = CreateBuffer(path.GetRawString(), &size);
    CubismMotion* tmpMotion = static_cast<CubismMotion*>(
        LoadMotion(buffer, size, name.GetRawString()));

    csmFloat32 fadeTime = _modelSetting->GetMotionFadeInTimeValue(group, i);
    if (fadeTime >= 0.0f) {
      tmpMotion->SetFadeInTime(fadeTime);
    }

    fadeTime = _modelSetting->GetMotionFadeOutTimeValue(group, i);
    if (fadeTime >= 0.0f) {
      tmpMotion->SetFadeOutTime(fadeTime);
    }
    tmpMotion->SetEffectIds(_eyeBlinkIds, _lipSyncIds);

    if (_motions[name] != NULL) {
      ACubismMotion::Delete(_motions[name]);
    }
    _motions[name] = tmpMotion;

    DeleteBuffer(buffer, path.GetRawString());
  }
}

void LAppModel::ReleaseMotionGroup(const csmChar* group) const {
  const csmInt32 count = _modelSetting->GetMotionCount(group);
  for (csmInt32 i = 0; i < count; i++) {
    csmString voice = _modelSetting->GetMotionSoundFileName(group, i);
    if (strcmp(voice.GetRawString(), "") != 0) {
      csmString path = voice;
      path = _modelHomeDir + path;
    }
  }
}

/**
 * @brief すべてのモーションデータの解放
 *
 * すべてのモーションデータを解放する。
 */
void LAppModel::ReleaseMotions() {
  for (csmMap<csmString, ACubismMotion*>::const_iterator iter =
           _motions.Begin();
       iter != _motions.End(); ++iter) {
    ACubismMotion::Delete(iter->Second);
  }

  _motions.Clear();
}

/**
 * @brief すべての表情データの解放
 *
 * すべての表情データを解放する。
 */
void LAppModel::ReleaseExpressions() {
  for (csmMap<csmString, ACubismMotion*>::const_iterator iter =
           _expressions.Begin();
       iter != _expressions.End(); ++iter) {
    ACubismMotion::Delete(iter->Second);
  }

  _expressions.Clear();
}

void LAppModel::Update() {
  const csmFloat32 deltaTimeSeconds = LAppPal::GetDeltaTime();
  _userTimeSeconds += deltaTimeSeconds;

  _dragManager->Update(deltaTimeSeconds);
  _dragX = _dragManager->GetX();
  _dragY = _dragManager->GetY();

  // モーションによるパラメータ更新の有無
  csmBool motionUpdated = false;

  //-----------------------------------------------------------------
  // Motion changes should be saved
  _model->LoadParameters();  // 前回セーブされた状態をロード
  motionUpdated = _motionManager->UpdateMotion(_model, deltaTimeSeconds);  
  _model->SaveParameters();
  //-----------------------------------------------------------------

  // まばたき
  if (!motionUpdated) {
    if (_eyeBlink != NULL) {
      // メインモーションの更新がないとき
      _eyeBlink->UpdateParameters(_model, deltaTimeSeconds);  // 目パチ
    }
  }

  // Expression changes are temporary
  if (_expressionManager != NULL) {
    _expressionManager->UpdateMotion(
        _model, deltaTimeSeconds);  // 表情でパラメータ更新（相対変化）
  }

  // Overwrite mouth for speaking
  if (LAppDelegate::GetInstance()->IsPlay()) {
    // reset to zero
    for (int i = 0; i < _mouthIds.GetSize(); i++) {
      _model->SetParameterValue(_mouthIds[i], 0);
    }
    
    // switch between mount1 and mount4 for speaking animation
    static csmFloat32 last_switch = _userTimeSeconds;
    csmFloat32 escape = _userTimeSeconds - last_switch;
    if (escape <= 0.25f) {
      _model->SetParameterValue(_mouthIds[0], 30);
      _model->SetParameterValue(_mouthIds[3], 0);
    }
    if (escape > 0.25f && escape <= 0.5f) {
      _model->SetParameterValue(_mouthIds[0], 0);
      _model->SetParameterValue(_mouthIds[3], 30);
    }
    if (escape > 0.5f) {
      last_switch = _userTimeSeconds;
    }
  }


  // ドラッグによる変化
  // ドラッグによる顔の向きの調整
  _model->AddParameterValue(_idParamAngleX,
                            _dragX * 30);  // -30から30の値を加える
  _model->AddParameterValue(_idParamAngleY, _dragY * 30);
  _model->AddParameterValue(_idParamAngleZ, _dragX * _dragY * -30);

  // ドラッグによる体の向きの調整
  _model->AddParameterValue(_idParamBodyAngleX,
                            _dragX * 10);  // -10から10の値を加える

  // ドラッグによる目の向きの調整
  _model->AddParameterValue(_idParamEyeBallX, _dragX);  // -1から1の値を加える
  _model->AddParameterValue(_idParamEyeBallY, _dragY);

  // 呼吸など
  if (_breath != NULL) {
    _breath->UpdateParameters(_model, deltaTimeSeconds);
  }

  // 物理演算の設定
  if (_physics != NULL) {
    _physics->Evaluate(_model, deltaTimeSeconds);
  }

  // リップシンクの設定
  if (_lipSync) {
    csmFloat32 value =
        0;  // リアルタイムでリップシンクを行う場合、システムから音量を取得して0〜1の範囲で値を入力します。

    for (csmUint32 i = 0; i < _lipSyncIds.GetSize(); ++i) {
      _model->AddParameterValue(_lipSyncIds[i], value, 0.8f);
    }
  }

  // ポーズの設定
  if (_pose != NULL) {
    _pose->UpdateParameters(_model, deltaTimeSeconds);
  }

  _model->Update();
}

CubismMotionQueueEntryHandle LAppModel::StartMotion(
    csmInt32 no, csmInt32 priority,
    ACubismMotion::FinishedMotionCallback onFinishedMotionHandler,
    bool IsIdle) {
  if (!IsIdle) LAppDelegate::GetInstance()->InMotion = true;
  if (priority == PriorityForce) {
    _motionManager->SetReservePriority(priority);
  } else if (!_motionManager->ReserveMotion(priority)) {
    if (_debugMode) {
      LAppPal::PrintLog("[APP]can't start motion.");
    }
    return InvalidMotionQueueEntryHandleValue;
  }
  const csmString fileName = _modelSetting->GetMotionFileName("All", no);

  if (fileName == "") {
    LAppPal::PrintLog("[APP]can't start motion with empty motions.");
    return InvalidMotionQueueEntryHandleValue;
  }

  // ex) idle_0
  csmString name = Utils::CubismString::GetFormatedString("%s_%d", "All", no);
  CubismMotion* motion =
      static_cast<CubismMotion*>(_motions[name.GetRawString()]);
  csmBool autoDelete = false;

  if (motion == NULL) {
    csmString path = fileName;
    path = _modelHomeDir + path;
    LAppPal::PrintLog("[APP]load motion: %s => [%s_%d] ", path.GetRawString(),
                      "All", no);
    csmByte* buffer;
    csmSizeInt size;
    buffer = CreateBuffer(path.GetRawString(), &size);
    motion = static_cast<CubismMotion*>(
        LoadMotion(buffer, size, NULL, onFinishedMotionHandler));
    csmFloat32 fadeTime = _modelSetting->GetMotionFadeInTimeValue("All", no);
    if (fadeTime >= 0.0f) {
      motion->SetFadeInTime(fadeTime);
    }

    fadeTime = _modelSetting->GetMotionFadeOutTimeValue("All", no);
    if (fadeTime >= 0.0f) {
      motion->SetFadeOutTime(fadeTime);
    }
    motion->SetEffectIds(_eyeBlinkIds, _lipSyncIds);
    autoDelete = true;  // 終了時にメモリから削除

    DeleteBuffer(buffer, path.GetRawString());
  } else {
    motion->SetFinishedMotionHandler(onFinishedMotionHandler);
  }

  // voice
  csmString voice = _modelSetting->GetMotionSoundFileName("All", no);

  LAppPal::PrintLog(LogLevel::Debug, "[Model]Start motion: [%s_%d]", "All", no);
  return _motionManager->StartMotionPriority(motion, autoDelete, priority);
}

CubismMotionQueueEntryHandle LAppModel::StartRandomMotion(
    csmInt32 priority,
    ACubismMotion::FinishedMotionCallback onFinishedMotionHandler) {
  if (_modelSetting->GetMotionCount("All") == 0) {
    return InvalidMotionQueueEntryHandleValue;
  }

  csmInt32 no = rand() % _modelSetting->GetMotionCount("All");

  return StartMotion(no, priority, onFinishedMotionHandler);
}

void LAppModel::DoDraw() {
  if (_model == NULL) {
    return;
  }

  GetRenderer<Rendering::CubismRenderer_OpenGLES2>()->DrawModel();
}

void LAppModel::UpdateViewPort() {
  if (_model == NULL) {
    return;
  }

  GetRenderer<Rendering::CubismRenderer_OpenGLES2>()->UpdateViewPort();
}


void LAppModel::Draw(CubismMatrix44& matrix) {
  if (_model == NULL) {
    return;
  }

  matrix.MultiplyByMatrix(_modelMatrix);

  GetRenderer<Rendering::CubismRenderer_OpenGLES2>()->SetMvpMatrix(&matrix);

  DoDraw();
}

csmBool LAppModel::HitTest(const csmChar* hitAreaName, csmFloat32 x,
                           csmFloat32 y) {
  // 透明時は当たり判定なし。
  if (_opacity < 1) {
    return false;
  }
  const csmInt32 count = _modelSetting->GetHitAreasCount();
  for (csmInt32 i = 0; i < count; i++) {
    if (strcmp(_modelSetting->GetHitAreaName(i), hitAreaName) == 0) {
      const CubismIdHandle drawID = _modelSetting->GetHitAreaId(i);
      return IsHit(drawID, x, y);
    }
  }
  return false;  // 存在しない場合はfalse
}

void LAppModel::SetDraggingState(bool state) {
  if (_dragging == state) {
    return;
  }
  LAppPal::PrintLog(LogLevel::Debug, "[Model]Set dragging state %d", state);
  if (state) {
    _expressionManager->StartMotionPriority(_presets["drag_on"], false,
                                            PriorityForce);
  } else {
    _expressionManager->StartMotionPriority(_presets["drag_off"], false,
                                            PriorityForce);
  }
  _dragging = state;
}

void LAppModel::SetExpression(const csmChar* expressionID) {
  ACubismMotion* motion = _expressions[expressionID];
  LAppPal::PrintLog(LogLevel::Debug, "[Model]Expression: [%s]", expressionID);

  if (motion != NULL) {
    SetExpression(motion);
  } else {
    LAppPal::PrintLog(LogLevel::Debug, "[Model]Expression[%s] is null ",
                      expressionID);
  }
}

void LAppModel::SetExpression(ACubismMotion* motion) {
  if (motion == nullptr) {
    LAppPal::PrintLog(LogLevel::Debug, "[Model]Expression is null ");
  }
  _expressionManager->StartMotionPriority(motion, true, PriorityForce);
}

void LAppModel::StartMotion(ACubismMotion* motion) {
  if (motion == nullptr) {
    return;
  }
  motion->SetFinishedMotionHandler(FinishedMotion);
  _motionManager->StartMotionPriority(motion, true, PriorityForce);
}

void LAppModel::SetRandomExpression() {
  if (_expressions.GetSize() == 0) {
    return;
  }

  csmInt32 no = rand() % _expressions.GetSize();
  csmMap<csmString, ACubismMotion*>::const_iterator map_ite;
  csmInt32 i = 0;
  for (map_ite = _expressions.Begin(); map_ite != _expressions.End();
       map_ite++) {
    if (i == no) {
      csmString name = (*map_ite).First;
      SetExpression(name.GetRawString());
      return;
    }
    i++;
  }
}

void LAppModel::ReloadRenderer() {
  DeleteRenderer();

  CreateRenderer();

  SetupTextures();
}

void LAppModel::SetupTextures() {
  for (csmInt32 modelTextureNumber = 0;
       modelTextureNumber < _modelSetting->GetTextureCount();
       modelTextureNumber++) {
    // テクスチャ名が空文字だった場合はロード・バインド処理をスキップ
    if (strcmp(_modelSetting->GetTextureFileName(modelTextureNumber), "") ==
        0) {
      continue;
    }

    // OpenGLのテクスチャユニットにテクスチャをロードする
    csmString texturePath =
        _modelSetting->GetTextureFileName(modelTextureNumber);
    texturePath = _modelHomeDir + texturePath;

    LAppTextureManager::TextureInfo* texture =
        LAppDelegate::GetInstance()
            ->GetTextureManager()
            ->CreateTextureFromPngFile(texturePath.GetRawString());
    const csmInt32 glTextueNumber = texture->id;

    // OpenGL
    GetRenderer<Rendering::CubismRenderer_OpenGLES2>()->BindTexture(
        modelTextureNumber, glTextueNumber);
  }

  GetRenderer<Rendering::CubismRenderer_OpenGLES2>()->IsPremultipliedAlpha(
      true);
}

void LAppModel::MotionEventFired(const csmString& eventValue) {
  CubismLogInfo("%s is fired on LAppModel!!", eventValue.GetRawString());
}

Csm::Rendering::CubismOffscreenSurface_OpenGLES2& LAppModel::GetRenderBuffer() {
  return _renderBuffer;
}
