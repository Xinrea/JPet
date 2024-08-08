/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at
 * https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppDefine.hpp"

#include <CubismFramework.hpp>
#include <string>

namespace LAppDefine {

using namespace Csm;

// 画面
const csmFloat32 ViewMaxScale = 2.0f;
const csmFloat32 ViewMinScale = 0.8f;

const csmFloat32 ViewLogicalLeft = -1.0f;
const csmFloat32 ViewLogicalRight = 1.0f;

const csmFloat32 ViewLogicalMaxLeft = -2.0f;
const csmFloat32 ViewLogicalMaxRight = 2.0f;
const csmFloat32 ViewLogicalMaxBottom = -2.0f;
const csmFloat32 ViewLogicalMaxTop = 2.0f;

// 相対パス
const csmChar *ResourcesPath = "resources/";
const csmChar *OptionImg = "imgs/message.png";

// モデル定義------------------------------------------
// モデルを配置したディレクトリ名の配列
// ディレクトリ名とmodel3.jsonの名前を一致させておくこと

const csmInt32 StartAudioNum = 9;
const csmInt32 IdleAudioNum = 7;
const csmInt32 TouchAudioNum = 5;

// 外部定義ファイル(json)と合わせる
const csmChar *HitAreaSetting = "HitAreaHairBall";
const csmChar *HitAreaNameHead = "AreaHead";
const csmChar *HitAreaNameBody = "AreaBody";
const int GunChangeList[2] = {14, 13};
const csmChar *HitAreaNameMouth = "AreaMouth";
const csmChar *HitAreaNameWanzi = "AreaWanzi";
const int EditChangeList[2] = {16, 15};
const csmChar *HitAreaNameArmL = "AreaArmL";
const csmChar *HitAreaNameArmR = "AreaArmR";
const int SleeveChangeList[2] = {11, 12};
const csmChar *HitAreaNameLegs = "AreaLegs";
const int SocksChangeList[2] = {2, 3};
const csmChar *HitAreaNameHat = "AreaHat";
const int HatChangeList[3] = {0, 1, 8};
const csmChar *HitAreaNameEarL = "AreaEarL";
const int EarLChangeList[2] = {4, 5};
const csmChar *HitAreaNameLegBelt = "AreaLegBelt";
const int LegBeltChangeList[2] = {6, 7};
const csmChar *HitAreaNameChange = "AreaChange";
const csmChar *HitAreaNameEyes = "AreaEyes";
const csmChar *HitAreaNameTail = "AreaTail";

// モーションの優先度定数
const csmInt32 PriorityNone = 0;
const csmInt32 PriorityIdle = 1;
const csmInt32 PriorityNormal = 2;
const csmInt32 PriorityForce = 3;

// デバッグ用ログの表示オプション
const csmBool DebugLogEnable = true;
const csmBool DebugTouchLogEnable = false;

// Frameworkから出力するログのレベル設定
const CubismFramework::Option::LogLevel CubismLoggingLevel =
    CubismFramework::Option::LogLevel_Verbose;

// デフォルトのレンダーターゲットサイズ
const csmInt32 DRenderTargetWidth = 512;
const csmInt32 DRenderTargetHeight = 512;
csmInt32 RenderTargetWidth = 512;
csmInt32 RenderTargetHeight = 512;
const csmInt32 modelWidth = 512;
const csmInt32 modelHeight = 1024;

const csmFloat32 AudioSpace = 3.0f;
const csmFloat32 AudioDepth = 2.0f;

// 音频
const csmChar *startAudioFile = "resources/audios/start.mp3";
const csmChar *dragAudioFile = "resources/audios/drag.mp3";
const csmChar *endAudioFile = "resources/audios/end.mp3";

std::wstring documentPath;
std::wstring execPath;
}  // namespace LAppDefine
