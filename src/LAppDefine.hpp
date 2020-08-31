/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */
#pragma once

#include <CubismFramework.hpp>
#include <string>

/**
* @brief  Sample Appで使用する定数
*
*/

#define VERSION L"200901"

namespace LAppDefine
{

using namespace Csm;

extern const csmFloat32 ViewMaxScale; ///< 拡大縮小率の最大値
extern const csmFloat32 ViewMinScale; ///< 拡大縮小率の最小値

extern const csmFloat32 ViewLogicalLeft;  ///< 論理的なビュー座標系の左端の値
extern const csmFloat32 ViewLogicalRight; ///< 論理的なビュー座標系の右端の値

extern const csmFloat32 ViewLogicalMaxLeft;   ///< 論理的なビュー座標系の左端の最大値
extern const csmFloat32 ViewLogicalMaxRight;  ///< 論理的なビュー座標系の右端の最大値
extern const csmFloat32 ViewLogicalMaxBottom; ///< 論理的なビュー座標系の下端の最大値
extern const csmFloat32 ViewLogicalMaxTop;    ///< 論理的なビュー座標系の上端の最大値

extern const csmChar *ResourcesPath;  ///< 素材パス
extern const csmChar *OptionImg;      ///< 背景画像ファイル
extern const csmChar *GearImageName;  ///< 歯車画像ファイル
extern const csmChar *PowerImageName; ///< 終了ボタン画像ファイル

// モデル定義--------------------------------------------
extern const csmChar *ModelDir[];   ///< モデルを配置したディレクトリ名の配列. ディレクトリ名とmodel3.jsonの名前を一致させておく.
extern const csmInt32 ModelDirSize; ///< モデルディレクトリ配列のサイズ

extern const csmInt32 StartAudioNum;
extern const csmInt32 IdleAudioNum;
extern const csmInt32 TouchAudioNum;

// 外部定義ファイル(json)と合わせる
extern const csmChar *MotionGroupIdle;    ///< アイドリング時に再生するモーションのリスト
extern const csmChar *MotionGroupTapHead; ///< 体をタップした時に再生するモーションのリスト
extern const csmChar *MotionGroupTapEar;
extern const csmChar* MotionGroupTapArm;
extern const csmChar *MotionGroupClothChange;
extern const csmChar *MotionGroupMouthChange;
extern const csmChar *MotionGroupFaceChange;
extern const csmChar* MotionGroupEyeChange;
extern const csmChar *MotionGroupPartChange;
extern const csmChar* MotionGroupSpecial;
// 外部定義ファイル(json)と合わせる
extern const csmChar *HitAreaNameHead;  ///< 当たり判定の[Head]タグ
extern const csmChar *HitAreaNameBody;  ///< 当たり判定の[Body]タグ
extern const int GunChangeList[2];
extern const csmChar *HitAreaNameMouth; ///< 当たり判定の[Mouth]タグ
extern const csmChar *HitAreaNameWanzi;
extern const int EditChangeList[2];
extern const csmChar *HitAreaNameArmL;
extern const csmChar *HitAreaNameArmR;
extern const int SleeveChangeList[2];
extern const csmChar *HitAreaNameLegs;
extern const int SocksChangeList[2];
extern const csmChar *HitAreaNameHat;
extern const int HatChangeList[3];
extern const csmChar* HitAreaNameEarL;
extern const int EarLChangeList[2];
extern const csmChar* HitAreaNameLegBelt;
extern const int LegBeltChangeList[2];
extern const csmChar* HitAreaNameChange;
extern const csmChar* HitAreaNameEyes;
extern const csmChar* HitAreaNameTail;

// モーションの優先度定数
extern const csmInt32 PriorityNone;   ///< モーションの優先度定数: 0
extern const csmInt32 PriorityIdle;   ///< モーションの優先度定数: 1
extern const csmInt32 PriorityNormal; ///< モーションの優先度定数: 2
extern const csmInt32 PriorityForce;  ///< モーションの優先度定数: 3

// デバッグ用ログの表示
extern const csmBool DebugLogEnable;      ///< デバッグ用ログ表示の有効・無効
extern const csmBool DebugTouchLogEnable; ///< タッチ処理のデバッグ用ログ表示の有効・無効

// Frameworkから出力するログのレベル設定
extern const CubismFramework::Option::LogLevel CubismLoggingLevel;

// デフォルトのレンダーターゲットサイズ
extern const csmInt32 DRenderTargetWidth;
extern const csmInt32 DRenderTargetHeight;
extern csmInt32 RenderTargetWidth;
extern csmInt32 RenderTargetHeight;
extern const csmInt32 modelWidth;
extern const csmInt32 modelHeight;

extern const csmFloat32 AudioSpace;
extern const csmFloat32 AudioDepth;

extern const csmChar *startAudioFile;
extern const csmChar *dragAudioFile;
extern const csmChar *endAudioFile;

extern std::string documentPath;
} // namespace LAppDefine
