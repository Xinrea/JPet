/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppDelegate.hpp"
#include <iostream>
#include <fstream>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <WinUser.h>
#include <shellapi.h>

#include "../resource.h"
#include "LAppView.hpp"
#include "LAppPal.hpp"
#include "LAppDefine.hpp"
#include "LAppLive2DManager.hpp"
#include "LAppTextureManager.hpp"
#include "TouchManager.hpp"
#include "ini.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "examples/imgui_impl_glfw.h"
#include "examples/imgui_impl_opengl3.h"

using namespace Csm;
using namespace std;
using namespace LAppDefine;
using namespace WinToastLib;

ImGuiStyle CherryTheme();

namespace
{
LAppDelegate *s_instance = NULL;
}

LAppDelegate *LAppDelegate::GetInstance()
{
    if (s_instance == NULL)
    {
        s_instance = new LAppDelegate();
    }

    return s_instance;
}

void LAppDelegate::ReleaseInstance()
{
    if (s_instance != NULL)
    {
        delete s_instance;
    }

    s_instance = NULL;
}

bool LAppDelegate::Initialize()
{
    if (DebugLogEnable)
    {
        LAppPal::PrintLog("START");
    }
    // 设置初始化
    INIReader reader("config.ini");
    if (reader.ParseError() == 0)
    {
        _iposX = reader.GetInteger("position", "x", 0);
        _iposY = reader.GetInteger("position", "y", 0);
        _leftUrl = reader.Get("shortcut", "left", "https://t.bilibili.com/");
        _upUrl = reader.Get("shortcut", "up", "https://space.bilibili.com/61639371/dynamic");
        _rightUrl = reader.Get("shortcut", "right", "https://live.bilibili.com/21484828");
        _volume = reader.GetInteger("audio", "volume", 50);
        _mute = reader.GetBoolean("audio", "mute", false);
    }

    // 音頻初始化
    _au = AudioManager::GetInstance();
    _au->Initialize();
    _au->SetMute(_mute);
    _au->SetVolume(static_cast<float>(_volume) / 10);

    // GLFWの初期化
    if (glfwInit() == GL_FALSE)
    {
        if (DebugLogEnable)
        {
            LAppPal::PrintLog("Can't initilize GLFW");
        }
        return GL_FALSE;
    }
    // 记录显示器分辨率尺寸
    GLFWmonitor *pr = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(pr);
    _mHeight = mode->height;
    _mWidth = mode->width;

    // Windowの生成_
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GL_TRUE);
    glfwWindowHint(GLFW_DECORATED, GL_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
    glfwWindowHint(GLFW_FLOATING, GL_TRUE);
    glfwWindowHint(GLFW_MAXIMIZED, GL_FALSE);
    _window = glfwCreateWindow(RenderTargetWidth, RenderTargetHeight, "JPet", NULL, NULL);
    GLFWcursor *cursor = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
    glfwSetCursor(_window, cursor);
    glfwSetWindowPos(_window, _iposX, _iposY);
    HWND hwnd = glfwGetWin32Window(_window);
    SetWindowLong(hwnd, GWL_EXSTYLE, WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_ACCEPTFILES);
    // 透明部分鼠标穿透
    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
    if (_window == NULL)
    {
        if (DebugLogEnable)
        {
            LAppPal::PrintLog("Can't create GLFW window.");
        }
        glfwTerminate();
        return GL_FALSE;
    }

    // 通知初始化
    WinToast::instance()->setAppName(L"JPet");
    const auto aumi = WinToast::configureAUMI(L"Xinrea", L"JPet", L"Jpet", L"Beta");
    WinToast::instance()->setAppUserModelId(aumi);
    WinToast::instance()->initialize();
    _notiHandler = new WinToastEventHandler();

    // Windowのコンテキストをカレントに設定
    glfwMakeContextCurrent(_window);
    glfwSwapInterval(1);
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGuiStyle style = CherryTheme();
    ImGui::StyleColorsDark(&style);

    io.Fonts->AddFontFromFileTTF("Resources/Font/Default.ttf", 20.0f, NULL, io.Fonts->GetGlyphRangesChineseFull());
    // Setup Platform/Renderer bindings
    ImGui_ImplGlfw_InitForOpenGL(_window, true);
    ImGui_ImplOpenGL3_Init();

    if (glewInit() != GLEW_OK)
    {
        if (DebugLogEnable)
        {
            LAppPal::PrintLog("Can't Initilize Glew.");
        }
        glfwTerminate();
        return GL_FALSE;
    }

    //テクスチャサンプリング設定
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    //透過設定
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    //コールバック関数の登録
    glfwSetMouseButtonCallback(_window, EventHandler::OnMouseCallBack);
    glfwSetCursorPosCallback(_window, EventHandler::OnMouseCallBack);
    glfwSetDropCallback(_window, EventHandler::OnDropCallBack);
    glfwSetWindowPosCallback(_window, EventHandler::OnWindowPosCallBack);

    // ウィンドウサイズ記憶
    int width, height;
    glfwGetWindowSize(LAppDelegate::GetInstance()->GetWindow(), &width, &height);
    _windowWidth = width;
    _windowHeight = height;

    //AppViewの初期化
    _view->Initialize();

    // Cubism SDK の初期化
    InitializeCubism();
    _au->Play3dSound("start");
    return GL_TRUE;
}

void LAppDelegate::Release()
{
    // Windowの削除
    glfwDestroyWindow(_window);

    glfwTerminate();

    delete _textureManager;
    delete _view;
    _au->Release();
    _au->ReleaseInstance();
    // リソースを解放
    LAppLive2DManager::ReleaseInstance();

    //Cubism SDK の解放
    CubismFramework::Dispose();
}

void LAppDelegate::Run()
{
    //メインループ
    while (glfwWindowShouldClose(_window) == GL_FALSE && !_isEnd)
    {
        int width, height;
        glfwGetWindowSize(LAppDelegate::GetInstance()->GetWindow(), &width, &height);
        if ((_windowWidth != width || _windowHeight != height) && width > 0 && height > 0)
        {
            //AppViewの初期化
            _view->Initialize();
            // スプライトサイズを再設定
            _view->ResizeSprite();
            // サイズを保存しておく
            _windowWidth = width;
            _windowHeight = height;

            // ビューポート変更
            glViewport(0, 0, width, height);
        }

        // 時間更新
        LAppPal::UpdateTime();
        int x, y;
        glfwGetWindowPos(_window, &x, &y);
        _au->Update(x, y, width, height, _mWidth, _mHeight);
        // 画面の初期化
        //glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearDepth(1.0);

        //描画更新
        _view->Render();
        // 设置界面
        if (_isSetting)
        {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(width, height));
            ImGui::Begin(u8"设置", &_isSetting, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
            if (ImGui::CollapsingHeader(u8"声音设置"))
            {
                ImGui::SliderInt(u8"-音量", &_volume, 0, 100);
                ImGui::Checkbox(u8"-静音", &_mute);
            }
            if (ImGui::CollapsingHeader(u8"功能设置"))
            {
                ImGui::InputText(u8"-左滑链接", &_leftUrl, 256);
                ImGui::InputText(u8"-上滑链接", &_upUrl, 256);
                ImGui::InputText(u8"-右滑链接", &_rightUrl, 256);
                ImGui::SliderFloat(u8"-呼出设置等待时间", &_timeSetting, 0.5f, 5.0f, "%.1fs");
            }
            if (ImGui::CollapsingHeader(u8"关于"))
            {
                ImGui::Text(u8"模型制作：Xinrea");
                ImGui::Text(u8"程序作者：Xinrea");
                ImGui::Text(u8"程序版本：20200314beta");
            }
            ImGui::End();

            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
        else
        {
            //TODO 设置窗口关闭后，只需要设置一次鼠标指针
            GLFWcursor *cursor = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
            glfwSetCursor(_window, cursor);
        }
        _au->SetVolume(static_cast<float>(_volume) / 10);
        _au->SetMute(_mute);

        // バッファの入れ替え
        glfwSwapBuffers(_window);

        // Poll for and process events
        glfwPollEvents();
    }
    ImGui_ImplGlfw_Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    // Release前保存配置
    int x, y;
    glfwGetWindowPos(_window, &x, &y);
    ofstream of;
    string mute = _mute ? "true" : "false";
    of.open("config.ini", ios::trunc);
    of << "[position]\n"
       << "x=" << x << "\ny=" << y << "\n[shortcut]\n"
       << "left=" << _leftUrl << "\nup=" << _upUrl << "\nright=" << _rightUrl << "\n[audio]\n"
       << "volume=" << _volume << "\nmute=" << mute;
    of.close();

    Release();

    LAppDelegate::ReleaseInstance();
}

LAppDelegate::LAppDelegate() : _cubismOption(),
                               _window(NULL),
                               _captured(false),
                               _mouseX(0.0f),
                               _mouseY(0.0f),
                               _pX(0),
                               _pY(0),
                               _isEnd(false),
                               _isMsg(false),
                               _iposX(0),
                               _iposY(0),
                               _cX(0),
                               _cY(0),
                               _mHeight(0),
                               _mWidth(0),
                               _leftUrl("https://t.bilibili.com/"),
                               _upUrl("https://space.bilibili.com/61639371/dynamic"),
                               _rightUrl("https://live.bilibili.com/21484828"),
                               _au(NULL),
                               _notiHandler(NULL),
                               _isSetting(false),
                               _timeSetting(1),
                               _holdTime(0),
                               _volume(50),
                               _mute(false),
                               _windowWidth(0),
                               _windowHeight(0)
{
    _view = new LAppView();
    _textureManager = new LAppTextureManager();
}

LAppDelegate::~LAppDelegate() = default;

void LAppDelegate::InitializeCubism()
{
    //setup cubism
    _cubismOption.LogFunction = LAppPal::PrintMessage;
    _cubismOption.LoggingLevel = LAppDefine::CubismLoggingLevel;
    Csm::CubismFramework::StartUp(&_cubismAllocator, &_cubismOption);

    //Initialize cubism
    CubismFramework::Initialize();

    //load model
    LAppLive2DManager::GetInstance();

    //default proj
    CubismMatrix44 projection;

    LAppPal::UpdateTime();

    _view->InitializeSprite();
}

void LAppDelegate::OnMouseCallBack(GLFWwindow *window, int button, int action, int modify)
{
    if (_view == NULL)
    {
        return;
    }
    if (GLFW_MOUSE_BUTTON_LEFT == button)
    {
        if (GLFW_PRESS == action)
        {
            if (!_isSetting)
            {
                _captured = true;
                glfwGetCursorPos(window, &_cX, &_cY);
                _au->Play3dSound("drag");
                _view->OnTouchesBegan(_mouseX, _mouseY);
                Notify();
            }
        }
        else if (GLFW_RELEASE == action)
        {
            if (_captured)
            {
                _captured = false;
                _view->OnTouchesEnded(_mouseX, _mouseY);
            }
        }
    }
    if (GLFW_MOUSE_BUTTON_RIGHT == button)
    {
        if (GLFW_PRESS == action && !_isSetting)
        {
            if (DebugLogEnable)
                LAppPal::PrintLog("Right Click Down");
            _holdTime = glfwGetTime();
            _isMsg = true;
            _pX = _mouseX;
            _pY = _mouseY;
        }
        else if (GLFW_PRESS == action && _isSetting)
        {
            _captured = true;
        }
        else if (GLFW_RELEASE == action && _isSetting)
        {
            _captured = false;
        }
        else if (GLFW_RELEASE == action && !_isSetting)
        {
            if (DebugLogEnable)
                LAppPal::PrintLog("Right Click Up");
            float dx = fabs(_mouseX - _pX);
            float dy = fabs(_mouseY - _pY);
            if (dx < 60 && dy < 60)
            {
                // 鼠标小范围移动
                double now = glfwGetTime();
                if (now - _holdTime > _timeSetting)
                    _isSetting = true;
            }
            else if (_mouseX > _pX && dx > dy)
                ShellExecute(NULL, "open", _rightUrl.c_str(), NULL, NULL, SW_SHOWNORMAL); //向右滑动
            else if (_mouseX < _pX && dx > dy)
                ShellExecute(NULL, "open", _leftUrl.c_str(), NULL, NULL, SW_SHOWNORMAL); //向左滑动
            else if (_mouseY < _pY && dy > dx)
                ShellExecute(NULL, "open", _upUrl.c_str(), NULL, NULL, SW_SHOWNORMAL); //向上滑动
            else if (_mouseY > _pY && dy > dx)
                _isEnd = true; //向下滑动
            _isMsg = false;
        }
    }
    return;
}

void LAppDelegate::OnMouseCallBack(GLFWwindow *window, double x, double y)
{
    _mouseX = static_cast<float>(x);
    _mouseY = static_cast<float>(y);
    if (_captured)
    {
        int xpos, ypos;
        glfwGetWindowPos(window, &xpos, &ypos);
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        glfwSetWindowPos(window, xpos + x - _cX, ypos + y - _cY);
        return;
    }
    if (_view == NULL)
    {
        return;
    }

    _view->OnTouchesMoved(_mouseX, _mouseY);
}

void LAppDelegate::OnDropCallBack(GLFWwindow *window, int path_count, const char *paths[])
{
    for (int i = 0; i < path_count; i++)
    {
        SHFILEOPSTRUCT shFile;
        ZeroMemory(&shFile, sizeof(shFile));
        shFile.pFrom = paths[i];
        shFile.wFunc = FO_DELETE;
        shFile.fFlags = FOF_SILENT | FOF_ALLOWUNDO | FOF_NOCONFIRMATION;
        SHFileOperation(&shFile);
    }
}

void LAppDelegate::OnWindowPosCallBack(GLFWwindow *window, int x, int y)
{
}

GLuint LAppDelegate::CreateShader()
{
    //バーテックスシェーダのコンパイル
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
    if (!CheckShader(vertexShaderId))
    {
        return 0;
    }

    //フラグメントシェーダのコンパイル
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
    if (!CheckShader(fragmentShaderId))
    {
        return 0;
    }

    //プログラムオブジェクトの作成
    GLuint programId = glCreateProgram();
    glAttachShader(programId, vertexShaderId);
    glAttachShader(programId, fragmentShaderId);

    // リンク
    glLinkProgram(programId);

    glUseProgram(programId);

    return programId;
}

bool LAppDelegate::CheckShader(GLuint shaderId)
{
    GLint status;
    GLint logLength;
    glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &logLength);
    if (logLength > 0)
    {
        GLchar *log = reinterpret_cast<GLchar *>(CSM_MALLOC(logLength));
        glGetShaderInfoLog(shaderId, logLength, &logLength, log);
        CubismLogError("Shader compile log: %s", log);
        CSM_FREE(log);
    }

    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE)
    {
        glDeleteShader(shaderId);
        return false;
    }

    return true;
}

ImGuiStyle CherryTheme()
{
// cherry colors, 3 intensities
#define HI(v) ImVec4(0.419f, 0.168f, 0.317f, v)
#define MED(v) ImVec4(0.756f, 0.443f, 0.564f, v)
#define LOW(v) ImVec4(0.945f, 0.701f, 0.698f, v)
// backgrounds (@todo: complete with BG_MED, BG_LOW)
#define BG(v) ImVec4(0.423f, 0.329f, 0.462f, v)
// text
#define TEXTC(v) ImVec4(0.860f, 0.930f, 0.890f, v)

    auto &style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Text] = TEXTC(0.78f);
    style.Colors[ImGuiCol_TextDisabled] = TEXTC(0.28f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.462f, 0.153f, 0.317f, 0.97f);
    style.Colors[ImGuiCol_PopupBg] = BG(0.9f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.31f, 0.31f, 1.00f, 0.00f);
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    style.Colors[ImGuiCol_FrameBg] = BG(1.00f);
    style.Colors[ImGuiCol_FrameBgHovered] = MED(0.78f);
    style.Colors[ImGuiCol_FrameBgActive] = MED(1.00f);
    style.Colors[ImGuiCol_TitleBg] = LOW(1.00f);
    style.Colors[ImGuiCol_TitleBgActive] = HI(1.00f);
    style.Colors[ImGuiCol_TitleBgCollapsed] = BG(0.75f);
    style.Colors[ImGuiCol_MenuBarBg] = BG(0.47f);
    style.Colors[ImGuiCol_ScrollbarBg] = BG(1.00f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.09f, 0.15f, 0.16f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = MED(0.78f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = MED(1.00f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.71f, 0.22f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_SliderGrab] = LOW(0.7f);
    style.Colors[ImGuiCol_SliderGrabActive] = HI(0.8f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.47f, 0.77f, 0.83f, 0.14f);
    style.Colors[ImGuiCol_ButtonHovered] = MED(0.86f);
    style.Colors[ImGuiCol_ButtonActive] = MED(1.00f);
    style.Colors[ImGuiCol_Header] = MED(0.76f);
    style.Colors[ImGuiCol_HeaderHovered] = MED(0.86f);
    style.Colors[ImGuiCol_HeaderActive] = HI(1.00f);
    style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.47f, 0.77f, 0.83f, 0.04f);
    style.Colors[ImGuiCol_ResizeGripHovered] = MED(0.78f);
    style.Colors[ImGuiCol_ResizeGripActive] = MED(1.00f);
    style.Colors[ImGuiCol_PlotLines] = TEXTC(0.63f);
    style.Colors[ImGuiCol_PlotLinesHovered] = MED(1.00f);
    style.Colors[ImGuiCol_PlotHistogram] = TEXTC(0.63f);
    style.Colors[ImGuiCol_PlotHistogramHovered] = MED(1.00f);
    style.Colors[ImGuiCol_TextSelectedBg] = MED(0.43f);
    // [...]
    style.Colors[ImGuiCol_ModalWindowDarkening] = BG(0.73f);

    style.WindowPadding = ImVec2(6, 4);
    style.WindowRounding = 0.0f;
    style.FramePadding = ImVec2(5, 2);
    style.FrameRounding = 3.0f;
    style.ItemSpacing = ImVec2(7, 1);
    style.ItemInnerSpacing = ImVec2(1, 1);
    style.TouchExtraPadding = ImVec2(0, 0);
    style.IndentSpacing = 6.0f;
    style.ScrollbarSize = 12.0f;
    style.ScrollbarRounding = 16.0f;
    style.GrabMinSize = 20.0f;
    style.GrabRounding = 2.0f;

    style.WindowTitleAlign.x = 0.50f;

    style.Colors[ImGuiCol_Border] = ImVec4(0.539f, 0.479f, 0.255f, 0.162f);
    style.FrameBorderSize = 0.0f;
    style.WindowBorderSize = 1.0f;
    return style;
}

void LAppDelegate::Notify()
{
    WinToastTemplate templ = WinToastTemplate(WinToastTemplate::Text02);
    templ.setTextField(L"轴伊开播了", WinToastTemplate::FirstLine);
    templ.setTextField(L"点击前往直播间", WinToastTemplate::SecondLine);
    WinToast::instance()->showToast(templ, _notiHandler);
}