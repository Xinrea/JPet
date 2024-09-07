/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at
 * https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppDelegate.hpp"
#include "AudioManager.hpp"
#include "MenuSprite.hpp"
#include "WinToastEventHandler.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <filesystem>
#include <mutex>
#include <shellapi.h>
#include <stdio.h>
#include <winbase.h>
#include <winuser.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <CommCtrl.h>
#include <commdlg.h>
#include <GLFW/glfw3native.h>
#include <VersionHelpers.h>

#define STBI_MSC_SECURE_CRT
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "DataManager.hpp"
#include "LAppDefine.hpp"
#include "LAppLive2DManager.hpp"
#include "LAppModel.hpp"
#include "LAppPal.hpp"
#include "LAppTextureManager.hpp"
#include "LAppView.hpp"
#include "PanelServer.hpp"
#include "PartStateManager.h"
#include "TaskScheduler.hpp"
#include "resource.h"

#include <wintoastlib.h>

#define WM_IAWENTRAY WM_USER + 5

using namespace Csm;
using namespace std;
using namespace LAppDefine;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK PreWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                               LPARAM lParam);
WNDPROC DefaultProc;

namespace {
LAppDelegate *s_instance = NULL;
}

LAppDelegate *LAppDelegate::GetInstance() {
  if (s_instance == NULL) {
    s_instance = new LAppDelegate();
  }

  return s_instance;
}

void LAppDelegate::ReleaseInstance() {
  if (s_instance != NULL) {
    delete s_instance;
  }

  s_instance = NULL;
}

bool LAppDelegate::Initialize() {
  LAppPal::PrintLog(LogLevel::Debug, "[LAppDelegate]START");
  WinToastLib::WinToast::instance()->setShortcutPolicy(WinToastLib::WinToast::SHORTCUT_POLICY_IGNORE);
  DataManager *dataManager = DataManager::GetInstance();
  // 设置初始化
  dataManager->GetWindowPos(&_iposX, &_iposY);
  dataManager->GetDisplay(&_scale, &Green, &isLimit);
  _followlist = dataManager->GetFollowList();
  dataManager->GetNotify(&DynamicNotify, &LiveNotify, &UpdateNotify);

  RenderTargetWidth = _scale * DRenderTargetWidth;
  RenderTargetHeight = _scale * DRenderTargetHeight;
  // 音频初始化
  _au = AudioManager::GetInstance();
  _au->Initialize();
  LAppPal::PrintLog(LogLevel::Debug, "[LAppDelegate]AudioManager Init");

  // GLFWの初期化
  if (glfwInit() == GL_FALSE) {
    if (DebugLogEnable) {
      LAppPal::PrintLog("[LAppDelegate]Can't initilize GLFW");
    }
    return GL_FALSE;
  }
  // 记录显示器分辨率尺寸
  GLFWmonitor *pr = glfwGetPrimaryMonitor();
  const GLFWvidmode *mode = glfwGetVideoMode(pr);
  _mHeight = mode->height;
  _mWidth = mode->width;

  // 获取当前路径，发送通知时图片地址需要为绝对路径
  wchar_t curPath[256];
  GetModuleFileName(GetModuleHandle(NULL), static_cast<LPWSTR>(curPath),
                    sizeof(curPath));
  _exePath = std::wstring(curPath);
  LAppPal::PrintLog(LogLevel::Debug, "[LAppDelegate]Get Execute Path");

  // Windowの生成_
  // 使用GLFW_DECORATED实现边框，会导致1703版本及以前，整个窗口鼠标穿透
  glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
  glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
  glfwWindowHint(GLFW_MAXIMIZED, GL_FALSE);
  glfwWindowHint(GLFW_ICONIFIED, GL_FALSE);
  glfwWindowHint(GLFW_FLOATING, GL_TRUE);
  glfwWindowHint(GLFW_DEPTH_BITS, 16);
  glfwWindowHint(GLFW_SAMPLES, 4);
  glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
  _window = glfwCreateWindow(RenderTargetWidth, RenderTargetHeight, "JPet",
                             NULL, NULL);

  if (_window == NULL) {
    LAppPal::PrintLog(LogLevel::Error, "[LAppDelegate]Can't create GLFW window.");
    glfwTerminate();
    return GL_FALSE;
  }

  // 为了避免1703版本前鼠标穿透的问题，在窗口创建完成后再修改为无边框
  glfwSetWindowAttrib(_window, GLFW_DECORATED, GLFW_FALSE);

  GLFWcursor *cursor = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
  glfwSetCursor(_window, cursor);
  glfwSetWindowPos(_window, _iposX, _iposY);

  HWND hwnd = glfwGetWin32Window(_window);
  _mainHwnd = hwnd;

  // 解决Win7下会在任务栏显示的bug
  HWND phwnd = CreateWindow(NULL,                      // window class name
                            TEXT("JPetParentWindow"),  // window caption
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                            CW_USEDEFAULT,  // initial x position
                            CW_USEDEFAULT,  // initial y position
                            CW_USEDEFAULT,  // initial x size
                            CW_USEDEFAULT,  // initial y size
                            NULL,           // parent window handle
                            NULL,           // window menu handle
                            NULL,           // program instance handle
                            NULL);          // creation parameters
  SetParent(hwnd, phwnd);

  SetWindowLong(hwnd, GWL_EXSTYLE,
                WS_EX_ACCEPTFILES | WS_EX_LAYERED | WS_EX_TOOLWINDOW);

  if (Green) {
    DWORD exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    exStyle &= ~WS_EX_TOOLWINDOW;
    SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
  } else {
    DWORD exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    exStyle |= WS_EX_TOOLWINDOW;
    SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
  }

  SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 255, LWA_COLORKEY);

  // 音频设定3d位置
  int x, y;
  glfwGetWindowPos(_window, &x, &y);
  _au->Update(x, y, RenderTargetWidth, RenderTargetHeight, _mWidth, _mHeight);

  // Windowのコンテキストをカレントに設定
  glfwMakeContextCurrent(_window);
  if (isLimit) {
    glfwSwapInterval(2);
  } else {
    glfwSwapInterval(1);
  }

  if (glewInit() != GLEW_OK) {
    LAppPal::PrintLog(LogLevel::Error, "[LAppDelegate]Can't Initilize Glew.");
    glfwTerminate();
    return GL_FALSE;
  }

  // テクスチャサンプリング設定
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

  // 透過設定
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

  glEnable(GL_MULTISAMPLE);

  // コールバック関数の登録
  glfwSetMouseButtonCallback(_window, EventHandler::OnMouseCallBack);
  glfwSetDropCallback(_window, EventHandler::OnDropCallBack);
  glfwSetCursorPosCallback(_window, EventHandler::OnMouseCallBack);
  glfwSetWindowPosCallback(_window, EventHandler::OnWindowPosCallBack);
  glfwSetWindowTrayCallback(_window, EventHandler::OnTrayClickCallBack);

  // ウィンドウサイズ記憶
  int width, height;
  glfwGetWindowSize(LAppDelegate::GetInstance()->GetWindow(), &width, &height);
  _windowWidth = width;
  _windowHeight = height;

  // 托盘图标初始化
  appIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
  nid.cbSize = sizeof(NOTIFYICONDATA);
  nid.hWnd = hwnd;
  nid.uID = IDI_ICON1;
  nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  nid.uCallbackMessage = WM_IAWENTRAY;
  nid.hIcon = appIcon;
  wcscpy(nid.szTip, TEXT("JPet - 桌面宠物轴伊"));
  Shell_NotifyIcon(NIM_ADD, &nid);

  // 设置窗口图标（绿幕模式下会显示在任务栏）
  SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)appIcon);

  // AppView 初始化
  _view->Initialize();

  // Cubism SDK 初始化
  InitializeCubism();

  srand(time(NULL));

  // Start panel server
  auto panelServer = PanelServer::GetInstance();
  panelServer->Start();

  // Init Game Panel
  _panel = new GamePanel(hwnd, GetModuleHandle(NULL));
  // 用户状态管理初始化
  _us = new UserStateManager(DynamicNotify, LiveNotify);
  _us->Init(_followlist, hwnd);

  // check update
  _us->CheckUpdate(UpdateNotify);

  // Init task scheduler and basic tasks
  TaskScheduler *ts = TaskScheduler::GetInstance();
  auto expTask = std::make_shared<ExpTask>();
  auto checkTask = std::make_shared<CheckTask>();
  ts->AddTask(expTask);
  ts->AddTask(checkTask);

  return GL_TRUE;
}

void LAppDelegate::SetGreen(bool green) {
  Green = green;
  HWND hwnd = glfwGetWin32Window(_window);
  if (Green) {
    DWORD exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    exStyle &= ~WS_EX_TOOLWINDOW;
    SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
  } else {
    DWORD exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    exStyle |= WS_EX_TOOLWINDOW;
    SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
  }
}

void LAppDelegate::SetLimit(bool limit) {
  isLimit = limit;
  if (limit)
    glfwSwapInterval(2);
  else
    glfwSwapInterval(1);
}

void LAppDelegate::Release() {
  // Windowの削除
  glfwDestroyWindow(_window);

  glfwTerminate();

  delete _textureManager;
  delete _view;
  _au->Release();
  _au->ReleaseInstance();
  _panel->Close();
  // リソースを解放
  LAppLive2DManager::ReleaseInstance();

  // Cubism SDK の解放
  CubismFramework::Dispose();

  LAppPal::ReleaseLog();
}

void LAppDelegate::Run() {
  static double initial_audio_idle_time = glfwGetTime();
  DataManager* dataManager = DataManager::GetInstance();
  LAppLive2DManager::GetInstance()->UpdateViewPort();
  
  // 随机播放启动语音
  _au->Play3dSound(AudioType::START, rand());

  // メインループ
  bool noskip = false;
  while (glfwWindowShouldClose(_window) == GL_FALSE && !_isEnd) {
    if (!_isShowing) {
      goto render_end;
    }
    noskip = !noskip;
    int width, height;
    glfwGetWindowSize(LAppDelegate::GetInstance()->GetWindow(), &width,
                      &height);

    static int x, y;
    if (noskip) {
      glfwGetWindowPos(_window, &x, &y);
      _au->Update(x, y, width, height, _mWidth, _mHeight);
    }

    if ((_windowWidth != width || _windowHeight != height) && width > 0 &&
        height > 0) {
      // AppViewの初期化
      _view->Initialize();
      // スプライトサイズを再設定
      _view->ResizeSprite();
      // サイズを保存しておく
      _windowWidth = width;
      _windowHeight = height;

      // ビューポート変更
      glViewport(0, 0, width, height);
      LAppLive2DManager::GetInstance()->UpdateViewPort();
    }

    // 闲置状态更新
    if (IsCount) {
      IdleCount++;
    }
    if (IdleCount > 60 * 6)  // 10s under 60fps
    {
      SetIdle();
      LAppPal::PrintLog(LogLevel::Debug, "[LAppDelegate]Idle On");
    }

    // 鼠标捕捉
    static double cx, cy;
    if (noskip) {
      glfwGetCursorPos(_window, &cx, &cy);
      // 非拖动状态下，跟随鼠标位置；拖动状态下，通过OnTouchMoved模拟物理效果
      if (!_captured && !InMotion && DataManager::GetInstance()->IsTracking()) {
        _view->OnTouchesMoved(static_cast<float>(cx), static_cast<float>(cy));
      }
    }

    // 画面の初期化
    if (!Green) {
      glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    } else {
      glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glClearDepth(1.0);

    // 時間更新
    LAppPal::UpdateTime();

    // 描画更新
    _view->Render();

    // バッファの入れ替え
    glfwSwapBuffers(_window);

    if (_need_snapshot.load()) {
      doSnapshot();
    }

  render_end:
    // Poll for and process events
    glfwPollEvents();

    if (dataManager->GetConfig<bool>("audio", "idle_audio", true)) {
      if (glfwGetTime() - initial_audio_idle_time > 30.0f) {
        initial_audio_idle_time = glfwGetTime();
        if (rand() % 100 >= 90) {
          _au->Play3dSound(AudioType::IDLE);
        }
      }
    }

    static float scale = _scale;
    static bool isShowing = false;

    // 设置界面
    if (_isSetting && !isShowing) {
      isShowing = true;
      // std::thread settingThread = SettingWindowThread();
      // settingThread.detach();
    }
    if (!_isSetting) isShowing = false;

    // 启动时刷新缩放，用于消除可能存在的边框残留
    // static bool scaleRefresh = true;
    // if (scaleRefresh) {
    //    if (!IsWindows8Point1OrGreater())glfwSetWindowSize(_window,
    //    RenderTargetWidth, RenderTargetHeight); scaleRefresh = false;
    //}

    if (scale != _scale) {
      scale = _scale;
      RenderTargetHeight = _scale * DRenderTargetHeight;
      RenderTargetWidth = _scale * DRenderTargetWidth;
      glfwSetWindowSize(_window, RenderTargetWidth, RenderTargetHeight);
      if (DebugLogEnable) LAppPal::PrintLog("[LAppDelegate] New Window Size");
    }
  }
  // Release前保存配置
  SaveSettings();
  Shell_NotifyIcon(NIM_DELETE, &nid);
  LAppPal::PrintLog(LogLevel::Debug, "[LAppDelegate]TrayICON Delete");
  Release();

  LAppDelegate::ReleaseInstance();
}

void LAppDelegate::SaveSettings() {
  // update window pos
  int x, y;
  glfwGetWindowPos(_window, &x, &y);
  DataManager *dataManager = DataManager::GetInstance();
  dataManager->UpdateWindowPos(x, y);

  // update display settings
  dataManager->UpdateDisplay(_scale, Green, isLimit);

  // update model part states
  PartStateManager::GetInstance()->SnapshotState();

  // update notify settings
  dataManager->UpdateNotify(DynamicNotify, LiveNotify, UpdateNotify);

  dataManager->Save();
  LAppPal::PrintLog(LogLevel::Debug, "[LAppDelegate]Setting Saved");
}

LAppDelegate::LAppDelegate()
    : _cubismOption(),
      _window(NULL),
      _captured(false),
      _mouseX(0.0f),
      _mouseY(0.0f),
      _pX(0),
      _pY(0),
      _isEnd(false),
      _iposX(400),
      _iposY(400),
      _cX(0),
      _cY(0),
      _mHeight(0),
      _mWidth(0),
      _au(NULL),
      _us(NULL),
      _isLive(false),
      _isSetting(false),
      _timeSetting(1),
      _holdTime(0),
      _windowWidth(0),
      _windowHeight(0) {
  _view = new LAppView();
  _textureManager = new LAppTextureManager();
}

LAppDelegate::~LAppDelegate() = default;

void LAppDelegate::InitializeCubism() {
  // setup cubism
  _cubismOption.LogFunction = LAppPal::PrintMessage;
  _cubismOption.LoggingLevel = LAppDefine::CubismLoggingLevel;
  Csm::CubismFramework::StartUp(&_cubismAllocator, &_cubismOption);

  // Initialize cubism
  CubismFramework::Initialize();

  // load model
  LAppLive2DManager::GetInstance();

  // default proj
  CubismMatrix44 projection;

  LAppPal::UpdateTime();

  _view->InitializeSprite();
}

void LAppDelegate::OnMouseCallBack(GLFWwindow *window, int button, int action,
                                   int modify) {
  if (_view == NULL) {
    return;
  }
  if (GLFW_MOUSE_BUTTON_LEFT == button) {
    SetNotIdle();
    if (GLFW_PRESS == action) {
      _captured = true;
      glfwGetCursorPos(window, &_cX, &_cY);
      _view->OnTouchesBegan(_mouseX, _mouseY);
      // set expression
    } else if (GLFW_RELEASE == action) {
      if (_captured) {
        _captured = false;
        _view->OnTouchesEnded(_mouseX, _mouseY);
        LAppModel *model = LAppLive2DManager::GetInstance()->GetModel(0);
        if (model != NULL) {
          model->SetDraggingState(false);
        }
      }
    }
  }
  if (GLFW_MOUSE_BUTTON_RIGHT == button) {
    if (GLFW_PRESS == action) {
      _holdTime = glfwGetTime();
      _pX = _mouseX;
      _pY = _mouseY;
      _view->GetMenuSprite()->Show();
      _menu_captured = true;
    } else if (GLFW_RELEASE == action) {
      auto selected = _view->GetMenuSprite()->GetSelected();
      _view->GetMenuSprite()->Hide();
      _menu_captured = false;
      if (selected == MenuSelect::None) {
        return;
      }
      // process item selected
      int item_index = 0;
      switch (selected) {
      case MenuSelect::UP: {
        item_index = 0;
        break;
      }
      case MenuSelect::RIGHT: {
        item_index = 1;
        break;
      }
      case MenuSelect::DOWN: {
        item_index = 2;
        break;
      }
      case MenuSelect::LEFT: {
        item_index = 3;
        break;
      }
      default: {
        item_index = 0;
      }
      }
      auto dm = DataManager::GetInstance();
      auto prefix = "shortcut." + std::to_string(item_index);
      // handle setting item
      int item_type = dm->GetWithDefault(prefix + ".type", 3);
      switch (item_type) {
      case 0: {
        // open app
        string param = dm->GetWithDefault(prefix + ".param", "");
        if (param.empty()) {
          break;
        }
        ShellExecute(NULL, L"open", LAppPal::StringToWString(param).c_str(),
                     NULL, NULL, SW_SHOWDEFAULT);
        break;
      }
      case 1: {
        // open folder
        string param = dm->GetWithDefault(prefix + ".param", "");
        if (param.empty()) {
          break;
        }
        ShellExecute(NULL, L"open", LAppPal::StringToWString(param).c_str(),
                     NULL, NULL, SW_SHOWDEFAULT);
        break;
      }
      case 2: {
        // open link
        string param = dm->GetWithDefault(prefix + ".param", "");
        if (param.empty()) {
          break;
        }
        ShellExecute(NULL, L"open", LAppPal::StringToWString(param).c_str(),
                     NULL, NULL, SW_SHOWDEFAULT);
        break;
      }
      case 3: {
        ShowPanel();
        break;
      }
      default: {
      }
      }
    }
  }
  return;
}

void LAppDelegate::OnMouseCallBack(GLFWwindow *window, double x, double y) {
  _mouseX = static_cast<float>(x);
  _mouseY = static_cast<float>(y);
  last_update_ = glfwGetTime();
  if (_captured) {
    LAppModel *model = LAppLive2DManager::GetInstance()->GetModel(0);
    if (model != NULL) {
      model->SetDraggingState(true);
    }
    int xpos, ypos;
    glfwGetWindowPos(window, &xpos, &ypos);
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    glfwSetWindowPos(window, xpos + x - _cX, ypos + y - _cY);
    // 简单模拟拖动时的物理效果
    double dx = x - _cX;
    if (dx > 0) {
      _view->OnTouchesMoved(x - 30 * dx, ypos - 2.0f * height / 3);
    }
    if (dx < 0) {
      _view->OnTouchesMoved(x - 30 * dx, ypos - 2.0f * height / 3);
    }
    return;
  }
  if (_menu_captured) {
    _view->UpdateMenu(x, y);
  }
}

void LAppDelegate::OnWindowPosCallBack(GLFWwindow *window, int x, int y) {}

// 托盘菜单设置
#define IDM_HIDE 2004
#define IDM_SET 2001
#define IDM_RESET 2002
#define IDM_EXIT 2003
#define IDM_PROJECT 2005

void LAppDelegate::OnTrayClickCallBack(GLFWwindow *window, int b, WPARAM w) {
  if (b == 2) {
    Menu();
  } else {
    switch (w) {
      case IDM_HIDE: {
        if (_isShowing)
          glfwHideWindow(_window);
        else
          glfwShowWindow(_window);
        _isShowing = !_isShowing;
        break;
      }
      case IDM_SET: {
        _isSetting = true;
        _panel->Show();
        break;
      }
      case IDM_EXIT: {
        _isEnd = true;
        break;
      }
      case IDM_RESET: {
        glfwSetWindowPos(window, 0, 0);
        break;
      }
      case IDM_PROJECT: {
        ShellExecute(NULL, L"open", L"https://pet.vjoi.cn", NULL, NULL, SW_SHOWNORMAL);
        break;
      }
      default:
        _isShowing = true;
        glfwShowWindow(_window);
        break;
    }
  }
}

GLuint LAppDelegate::CreateShader() {
  // バーテックスシェーダのコンパイル
  GLuint vertexShaderId = glCreateShader(GL_VERTEX_SHADER);
  const char *vertexShader =
      "#version 120\n"
      "attribute vec3 position;"
      "attribute vec2 uv;"
      "varying vec2 vuv;"
      "void main(void){"
      "    gl_Position = vec4(position, 1.0);"
      "    vuv = uv;"
      "}";
  glShaderSource(vertexShaderId, 1, &vertexShader, NULL);
  glCompileShader(vertexShaderId);
  if (!CheckShader(vertexShaderId)) {
    return 0;
  }

  // フラグメントシェーダのコンパイル
  GLuint fragmentShaderId = glCreateShader(GL_FRAGMENT_SHADER);
  const char *fragmentShader =
      "#version 120\n"
      "varying vec2 vuv;"
      "uniform sampler2D texture;"
      "uniform vec4 baseColor;"
      "void main(void){"
      "    gl_FragColor = texture2D(texture, vuv) * baseColor;"
      "}";
  glShaderSource(fragmentShaderId, 1, &fragmentShader, NULL);
  glCompileShader(fragmentShaderId);
  if (!CheckShader(fragmentShaderId)) {
    return 0;
  }

  // プログラムオブジェクトの作成
  GLuint programId = glCreateProgram();
  glAttachShader(programId, vertexShaderId);
  glAttachShader(programId, fragmentShaderId);

  // リンク
  glLinkProgram(programId);

  glUseProgram(programId);

  return programId;
}

bool LAppDelegate::CheckShader(GLuint shaderId) {
  GLint status;
  GLint logLength;
  glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &logLength);
  if (logLength > 0) {
    GLchar *log = reinterpret_cast<GLchar *>(CSM_MALLOC(logLength));
    glGetShaderInfoLog(shaderId, logLength, &logLength, log);
    CubismLogError("Shader compile log: %s", log);
    CSM_FREE(log);
  }

  glGetShaderiv(shaderId, GL_COMPILE_STATUS, &status);
  if (status == GL_FALSE) {
    glDeleteShader(shaderId);
    return false;
  }

  return true;
}

void LAppDelegate::Menu() {
  // TODO using a seperate window as menu
  POINT p;
  GetCursorPos(&p);
  HMENU hMenu;
  hMenu = CreatePopupMenu();
  AppendMenu(hMenu, MF_STRING | MF_GRAYED, 0, (L"Version " + LAppPal::StringToWString((VERSION))).c_str());
  AppendMenu(hMenu, MF_STRING, IDM_PROJECT, TEXT("前往项目主页"));
  AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
  if (_isShowing) {
    AppendMenu(hMenu, MF_STRING, IDM_HIDE, TEXT("隐藏"));
  } else {
    AppendMenu(hMenu, MF_STRING, IDM_HIDE, TEXT("显示"));
  }
  AppendMenu(hMenu, MF_STRING, IDM_RESET, TEXT("重置位置"));
  AppendMenu(hMenu, MF_STRING, IDM_SET, TEXT("设置"));
  AppendMenu(hMenu, MF_STRING, IDM_EXIT, TEXT("退出"));
  SetForegroundWindow(glfwGetWin32Window(_window));
  TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, p.x, p.y, NULL,
                 glfwGetWin32Window(_window), NULL);
}

std::thread LAppDelegate::MenuThread() {
  return std::thread(&LAppDelegate::Menu, this);
}

void LAppDelegate::ShowPanel() {
  if (_panel) _panel->Show();
}

void LAppDelegate::ForceShowPanel() {
  if (_panel) _panel->ForceShow();
}

void LAppDelegate::Snapshot() {
  _need_snapshot.store(true);
  std::unique_lock<std::mutex> lock(_mtx);
  _cv.wait(lock, [&]{return !_need_snapshot.load();});

  const wstring filepath = LAppDefine::documentPath + L"/snapshot.png";
  OPENFILENAME ofn; // Common dialog box structure
  wchar_t szFile[260] = L"snapshot.png\0"; // Buffer for file name

  HWND hwnd = glfwGetWin32Window(_window);

  ZeroMemory(&ofn, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = hwnd;
  ofn.lpstrFile = szFile;
  ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
  ofn.lpstrFilter = L"PNG Files (*.png)\0*.png\0";
  ofn.nFilterIndex = 1;
  ofn.lpstrFileTitle = NULL;
  ofn.nMaxFileTitle = 0;
  ofn.lpstrInitialDir = NULL;
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

  if (GetSaveFileName(&ofn) == TRUE) {
    // Use ofn.lpstrFile here to open the file for writing
    CopyFile(filepath.c_str(), ofn.lpstrFile, FALSE);
    std::filesystem::remove(filepath);
  }
}

void LAppDelegate::doSnapshot() {
  LAppPal::PrintLog(LogLevel::Info, "[LAppDelegate]Do snapshot");
  const wstring filepath = LAppDefine::documentPath + L"/snapshot.png";
  int width, height;
  glfwGetFramebufferSize(_window, &width, &height);
  GLsizei nrChannels = 4;
  GLsizei stride = nrChannels * width;
  stride += (stride % 4) ? (4 - stride % 4) : 0;
  GLsizei bufferSize = stride * height;
  std::vector<char> buffer(bufferSize);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadBuffer(GL_FRONT);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());
  stbi_flip_vertically_on_write(true);
  stbi_write_png(LAppPal::WStringToString(filepath).c_str(), width, height, nrChannels, buffer.data(), stride);
  _need_snapshot.store(false);
  _cv.notify_one();
}

void LAppDelegate::OnDropCallBack(GLFWwindow *window, int path_count,
                                  const char *paths[]) {
  if (!DataManager::GetInstance()->GetDropFile()) {
    return;
  }
  if (path_count == 0 || paths == nullptr) {
    return;
  }
  auto move_to_recycle = [](const wstring &path) {
    SHFILEOPSTRUCTW fileOp = {0};
    fileOp.wFunc = FO_DELETE;
    fileOp.fFlags = FOF_ALLOWUNDO;
    std::wstring doubleNullTerminatedPath = path + L'\0';
    fileOp.pFrom = doubleNullTerminatedPath.c_str();
    return SHFileOperationW(&fileOp);
  };
  // start a temp thread to delete files avoid blocking ui
  vector<wstring> wpaths;
  for (size_t i = 0; i < path_count; i++) {
    auto current_path = LAppPal::StringToWString(paths[i]);
    wpaths.push_back(current_path);
  }
  auto delete_work = [&](vector<wstring> paths) {
    LAppPal::PrintLog(LogLevel::Debug, "[LAppDelegate]Deleteing %llu files", paths.size());
    for (const wstring& p : paths) {
      move_to_recycle(p);
    }
    wchar_t content_buffer[128];
    swprintf_s(content_buffer, L"将 %llu 个文件/文件夹移动到了回收站",
               paths.size());
    _us->Notify(L"文件回收", wstring(content_buffer),
                new WinToastEventHandler(""));
  };
  std::thread(delete_work, wpaths).detach();
}
