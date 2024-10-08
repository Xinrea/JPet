﻿/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at
 * https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <Windows.h>
#include <shellapi.h>
#include <atomic>
#include <condition_variable>

#include <string>

#include "AudioManager.hpp"
#include "GamePanel.hpp"
#include "LAppAllocator.hpp"
#include "UserStateManager.h"


class LAppView;
class LAppTextureManager;

/**
 * @brief   アプリケーションクラス。
 *   Cubism SDK の管理を行う。
 */
class LAppDelegate {
 public:
  /**
   * @brief   クラスのインスタンス（シングルトン）を返す。<br>
   *           インスタンスが生成されていない場合は内部でインスタンを生成する。
   *
   * @return  クラスのインスタンス
   */
  static LAppDelegate *GetInstance();

  /**
   * @brief   クラスのインスタンス（シングルトン）を解放する。
   *
   */
  static void ReleaseInstance();

  /**
   * @brief   APPに必要なものを初期化する。
   */
  bool Initialize();

  /**
   * @brief   解放する。
   */
  void Release();

  /**
   * @brief   実行処理。
   */
  void Run();

  /**
   * @brief   OpenGL用 glfwSetMouseButtonCallback用関数。
   *
   * @param[in]       window            コールバックを呼んだWindow情報
   * @param[in]       button            ボタン種類
   * @param[in]       action            実行結果
   * @param[in]       modify
   */
  void OnMouseCallBack(GLFWwindow *window, int button, int action, int modify);

  /**
   * @brief   OpenGL用 glfwSetCursorPosCallback用関数。
   *
   * @param[in]       window            コールバックを呼んだWindow情報
   * @param[in]       x                 x座標
   * @param[in]       y                 x座標
   */
  void OnMouseCallBack(GLFWwindow *window, double x, double y);

  // 文件拖放的CallBack，用于获取拖放文件的路径和名称
  void OnDropCallBack(GLFWwindow *window, int path_count, const char *paths[]);

  void OnWindowPosCallBack(GLFWwindow *window, int x, int y);

  // 修改glfw添加的新CallBack，用于响应托盘事件
  void OnTrayClickCallBack(GLFWwindow *window, int b, WPARAM w);

  /**
   * @brief シェーダーを登録する。
   */
  GLuint CreateShader();

  /**
   * @brief   Window情報を取得する。
   */
  GLFWwindow *GetWindow() { return _window; }

  /**
   * @brief   View情報を取得する。
   */
  LAppView *GetView() { return _view; }

  /**
   * @brief   アプリケーションを終了するかどうか。
   */
  bool GetIsEnd() { return _isEnd; }

  bool IsHover() { return last_update_ > 0 && ((glfwGetTime() - last_update_) <= 3); }

  AudioManager *GetAudioManager() { return _au; }
  bool IsPlay() { return _au->IsPlay(); }

  void SetIsSetting(bool s) { _isSetting = s; }

  UserStateManager *GetUserStateManager() { return _us; }

  float GetScale() { return _scale; }
  void SetScale(float s) { _scale = s; }

  float GetTimeSetting() { return _timeSetting; }
  void SetTimeSetting(float t) { _timeSetting = t; }

  // 设置绿幕状态，涉及窗口显示模式的切换
  void SetGreen(bool green);

  void SetLimit(bool limit);

  void SaveSettings();

  void ShowPanel();

  void ForceShowPanel();

  void AddWatch(const std::string &s) {
    if (_us) _us->AddWatcher(s);
  }
  void RemoveWatch(const std::string &s) {
    if (_us) _us->RemoveWatcher(s);
  }

  bool LiveNotify = true;
  bool DynamicNotify = true;
  bool UpdateNotify = true;
  bool Green = false;
  bool isLimit = false;
  bool InMotion = false;

  bool IsIdle = true;
  bool IsCount = false;
  int IdleCount = 0;

  void SetNotIdle() {
    IsIdle = false;
    IdleCount = 0;
    IsCount = true;
  }

  void SetIdle() {
    IsIdle = true;
    IdleCount = 0;
    IsCount = false;
  }

  /**
   * @brief   アプリケーションを終了する。
   */
  void AppEnd() { _isEnd = true; }

  LAppTextureManager *GetTextureManager() { return _textureManager; }
  
  /**
   * @brief take a snapshot
   */
  void Snapshot();

 private:
  /**
   * @brief   コンストラクタ
   */
  LAppDelegate();

  /**
   * @brief   デストラクタ
   */
  ~LAppDelegate();

  /**
   * @brief   Cubism SDK の初期化
   */
  void InitializeCubism();

  /**
   * @brief   CreateShader内部関数 エラーチェック
   */
  bool CheckShader(GLuint shaderId);

  void Menu();
  
  std::thread MenuThread();

  
  void doSnapshot();

  LAppAllocator _cubismAllocator;              ///< Cubism SDK Allocator
  Csm::CubismFramework::Option _cubismOption;  ///< Cubism SDK Option
  GLFWwindow *_window;                         ///< OpenGL ウィンドウ
  LAppView *_view;                             ///< View情報
  HWND _setHwnd;
  HWND _mainHwnd;
  bool _captured;  ///< クリックしているか
  bool _menu_captured = false;
  float _mouseX;   ///< マウスX座標
  float _mouseY;   ///< マウスY座標
  float _pX;
  float _pY;
  double _cX, _cY;
  bool _isEnd;  ///< APP終了しているか
  bool _isShowing = true;
  bool _isLive;
  int _mWidth, _mHeight;
  std::wstring _exePath;
  std::vector<std::string> _followlist;
  AudioManager *_au;
  UserStateManager *_us;
  NOTIFYICONDATA nid;

  GamePanel *_panel;

  LAppTextureManager *_textureManager;  ///< テクスチャマネージャー

  // Config Part
  int _iposX, _iposY;
  bool _isSetting;
  float _timeSetting;
  double _holdTime;
  float _scale = 1.0f;

  int _windowWidth;   ///< Initialize関数で設定したウィンドウ幅
  int _windowHeight;  ///< Initialize関数で設定したウィンドウ高さ

  HICON appIcon;

  atomic_bool _need_snapshot;
  std::mutex _mtx;
  std::condition_variable _cv;

  int last_update_ = 0;
};

class EventHandler {
 public:
  /**
   * @brief   glfwSetMouseButtonCallback用コールバック関数。
   */
  static void OnMouseCallBack(GLFWwindow *window, int button, int action,
                              int modify) {
    LAppDelegate::GetInstance()->OnMouseCallBack(window, button, action,
                                                 modify);
  }

  static void OnDropCallBack(GLFWwindow *window, int path_count, const char* paths[]) {
    LAppDelegate::GetInstance()->OnDropCallBack(window, path_count, paths);
  }

  /**
   * @brief   glfwSetCursorPosCallback用コールバック関数。
   */
  static void OnMouseCallBack(GLFWwindow *window, double x, double y) {
    LAppDelegate::GetInstance()->OnMouseCallBack(window, x, y);
  }

  static void OnWindowPosCallBack(GLFWwindow *window, int x, int y) {
    LAppDelegate::GetInstance()->OnWindowPosCallBack(window, x, y);
  }
  static void OnTrayClickCallBack(GLFWwindow *window, int b, WPARAM w) {
    LAppDelegate::GetInstance()->OnTrayClickCallBack(window, b, w);
  }
};
