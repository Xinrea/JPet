/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at
 * https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppLive2DManager.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <Rendering/CubismRenderer.hpp>
#include <functional>
#include <random>
#include <string>

#include "LAppDefine.hpp"
#include "LAppDelegate.hpp"
#include "LAppModel.hpp"
#include "LAppPal.hpp"
#include "LAppView.hpp"
#include "ModelManager.hpp"
#include "PartStateManager.h"

using namespace Csm;
using namespace LAppDefine;
using namespace std;

namespace {
LAppLive2DManager* s_instance = NULL;

void FinishedMotion(ACubismMotion* self) {
  LAppDelegate::GetInstance()->InMotion = false;
  LAppPal::PrintLog("Motion Finished: %x", self);
}
void PartFinishedMotion(ACubismMotion* self) {
  LAppDelegate::GetInstance()->InMotion = false;
  LAppPal::PrintLog("Part Change Finished: %x", self);
  PartStateManager::GetInstance()->SaveState();
}
}  // namespace

LAppLive2DManager* LAppLive2DManager::GetInstance() {
  if (s_instance == NULL) {
    s_instance = new LAppLive2DManager();
  }

  return s_instance;
}

void LAppLive2DManager::ReleaseInstance() {
  if (s_instance != NULL) {
    delete s_instance;
  }

  s_instance = NULL;
}

LAppLive2DManager::LAppLive2DManager()
    : _viewMatrix(NULL), _sceneIndex(0), _isNew(true), _mouthCount(0) {
  InitScene();
}

LAppLive2DManager::~LAppLive2DManager() { ReleaseAllModel(); }

void LAppLive2DManager::ReleaseAllModel() {
  for (csmUint32 i = 0; i < _models.GetSize(); i++) {
    delete _models[i];
  }

  _models.Clear();
}

LAppModel* LAppLive2DManager::GetModel(csmUint32 no) const {
  if (no < _models.GetSize()) {
    return _models[no];
  }

  return NULL;
}

void LAppLive2DManager::OnDrag(csmFloat32 x, csmFloat32 y) const {
  // if |x| or |y| > 1, should make it in range [-1,1] to make model looks
  // natural
  if (abs(x) > 1) {
    y = y / abs(x);
    x = x / abs(x);
  }
  if (y > 1) {
    x = x / abs(y);
    y = y / abs(y);
  }
  for (csmUint32 i = 0; i < _models.GetSize(); i++) {
    LAppModel* model = GetModel(i);

    model->SetDragging(x, y);
  }
}

void LAppLive2DManager::OnFollow() {
  if (DebugLogEnable) {
    LAppPal::PrintLog("[APP]New Follow");
  }
  _models[0]->StartRandomMotion(PriorityForce, FinishedMotion);
}

void LAppLive2DManager::PlayTouchAudio(string filename) {
  std::random_device rd;
  std::mt19937 generator(rd());
  std::uniform_int_distribution<int> d1(1, 100);
  if (d1(generator) > 70) {
    // 播放特定语音
    AudioManager::GetInstance()->Play3dSound("resources/audios/" + filename);
  } else {
    // 播放一般语音
    PlayRandomTouchAudio();
  }
}

void LAppLive2DManager::PlayRandomTouchAudio() {
  std::random_device rd;
  std::mt19937 generator(rd());
  std::uniform_int_distribution<int> d(1, TouchAudioNum);
  AudioManager::GetInstance()->Play3dSound("resources/audios/r0" +
                                           to_string(d(generator)) + ".mp3");
}

void LAppLive2DManager::OnTap(csmFloat32 x, csmFloat32 y) {
  // TODO: 添加动作表情和声音
  LAppPal::PrintLog(LogLevel::Debug,
                    "[Live2DManager]Tap point: {x:%.2f y:%.2f}", x, y);
  CubismMotionQueueEntryHandle hr;
  for (csmUint32 i = 0; i < _models.GetSize(); i++) {
    if (_models[i]->HitTest(HitAreaSetting, x, y)) {
      LAppPal::PrintLog(LogLevel::Debug, "[Live2DManager]Hit area: [%s]",
                        HitAreaSetting);
      LAppDelegate::GetInstance()->ShowPanel();
    }
  }
}

void LAppLive2DManager::OnUpdate() const {
  CubismMatrix44 projection;
  int width, height;
  glfwGetWindowSize(LAppDelegate::GetInstance()->GetWindow(), &width, &height);
  // float ratio = static_cast<float>(modelWidth) / width;
  // float ty = (static_cast<float>(height) - modelHeight) / height;
  // projection.Scale(1.0f, static_cast<float>(width) /
  // static_cast<float>(height)); projection.ScaleRelative(ratio,ratio);
  // projection.TranslateY(-ty);

  if (_viewMatrix != NULL) {
    //    projection.MultiplyByMatrix(_viewMatrix);
  }

  const CubismMatrix44 saveProjection = projection;
  csmUint32 modelCount = _models.GetSize();
  for (csmUint32 i = 0; i < modelCount; ++i) {
    LAppModel* model = GetModel(i);
    projection = saveProjection;
    projection.Scale(1, 1);
    model->Update();
    model->Draw(projection);  ///< 参照渡しなのでprojectionは変質する
  }
}

void LAppLive2DManager::InitScene() {
  // ModelDir[]に保持したディレクトリ名から
  // model3.jsonのパスを決定する.
  // ディレクトリ名とmodel3.jsonの名前を一致させておくこと.
  const ModelManager* modelManager = ModelManager::GetInstance();
  std::string modelPath = ResourcesPath + modelManager->modelName + "/";
  std::string modelJsonName = modelManager->modelFileName;

  ReleaseAllModel();
  _models.PushBack(new LAppModel());
  _models[0]->LoadAssets(modelPath.c_str(), modelJsonName.c_str());

  /*
   * モデル半透明表示を行うサンプルを提示する。
   * ここでUSE_RENDER_TARGET、USE_MODEL_RENDER_TARGETが定義されている場合
   * 別のレンダリングターゲットにモデルを描画し、描画結果をテクスチャとして別のスプライトに張り付ける。
   */
  {
#if defined(USE_RENDER_TARGET)
    // LAppViewの持つターゲットに描画を行う場合、こちらを選択
    LAppView::SelectTarget useRenderTarget =
        LAppView::SelectTarget_ViewFrameBuffer;
#elif defined(USE_MODEL_RENDER_TARGET)
    // 各LAppModelの持つターゲットに描画を行う場合、こちらを選択
    LAppView::SelectTarget useRenderTarget =
        LAppView::SelectTarget_ModelFrameBuffer;
#else
    // デフォルトのメインフレームバッファへレンダリングする(通常)
    LAppView::SelectTarget useRenderTarget = LAppView::SelectTarget_None;
#endif

#if defined(USE_RENDER_TARGET) || defined(USE_MODEL_RENDER_TARGET)
    // モデル個別にαを付けるサンプルとして、もう1体モデルを作成し、少し位置をずらす
    _models.PushBack(new LAppModel());
    _models[1]->LoadAssets(modelPath.c_str(), modelJsonName.c_str());
    _models[1]->GetModelMatrix()->TranslateX(0.2f);
#endif

    LAppDelegate::GetInstance()->GetView()->SwitchRenderingTarget(
        useRenderTarget);

    // 別レンダリング先を選択した際の背景クリア色
    float clearColor[3] = {0.0f, 0.0f, 0.0f};
    LAppDelegate::GetInstance()->GetView()->SetRenderTargetClearColor(
        clearColor[0], clearColor[1], clearColor[2]);
  }
}

csmUint32 LAppLive2DManager::GetModelNum() const { return _models.GetSize(); }
