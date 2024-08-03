/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at
 * https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include <CubismFramework.hpp>
#include <fstream>
#include <string>

enum class LogLevel { Debug, Info, Warn, Error };

/**
 * @brief プラットフォーム依存機能を抽象化する Cubism Platform Abstraction
 * Layer.
 *
 * ファイル読み込みや時刻取得等のプラットフォームに依存する関数をまとめる
 *
 */
class LAppPal {
 public:
  /**
   * @brief ファイルをバイトデータとして読み込む
   *
   * ファイルをバイトデータとして読み込む
   *
   * @param[in]   filePath    読み込み対象ファイルのパス
   * @param[out]  outSize     ファイルサイズ
   * @return                  バイトデータ
   */
  static Csm::csmByte* LoadFileAsBytes(const std::string& filePath,
                                       Csm::csmSizeInt* outSize);

  /**
   * @brief バイトデータを解放する
   *
   * バイトデータを解放する
   *
   * @param[in]   byteData    解放したいバイトデータ
   */
  static void ReleaseBytes(const Csm::csmByte* byteData);

  /**
   * @biref   デルタ時間（前回フレームとの差分）を取得する
   *
   * @return  デルタ時間[ms]
   *
   */
  static Csm::csmFloat32 GetDeltaTime();

  static void UpdateTime();

  /**
   * @brief ログを出力する
   *
   * ログを出力する
   *
   * @param[in]   format  書式付文字列
   * @param[in]   ...     (可変長引数)文字列
   *
   */
  static void PrintLog(const Csm::csmChar* format, ...);

  static void PrintLog(LogLevel level, const Csm::csmChar* format, ...);

  /**
   * @brief メッセージを出力する
   *
   * メッセージを出力する
   *
   * @param[in]   message  文字列
   *
   */
  static void PrintMessage(const Csm::csmChar* message);

  /**
   * @brief 释放日志文件
   */
  static void ReleaseLog() { s_logFile.close(); }

  static std::wstring StringToWString(const std::string& s);

  static std::string WStringToString(const std::wstring& str);

  /**
   * @brief ease function
   * @param x [0,100]
   * @return [0,100]
   */
  static double EaseInOut(int x);

 private:
  static double s_currentFrame;
  static double s_lastFrame;
  static double s_deltaTime;
  static std::fstream s_logFile;
};
