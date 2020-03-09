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
#include "LAppView.hpp"
#include "LAppPal.hpp"
#include "LAppDefine.hpp"
#include "LAppLive2DManager.hpp"
#include "LAppTextureManager.hpp"
#include "TouchManager.hpp"
#include "ini.h"

using namespace Csm;
using namespace std;
using namespace LAppDefine;

namespace {
    LAppDelegate* s_instance = NULL;
}

LAppDelegate* LAppDelegate::GetInstance()
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
        _scale = reader.GetFloat("display","scale",1.0f);
    }

    // GLFWの初期化
    if (glfwInit() == GL_FALSE)
    {
        if (DebugLogEnable)
        {
            LAppPal::PrintLog("Can't initilize GLFW");
        }
        return GL_FALSE;
    }

    // Windowの生成_
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GL_TRUE);
    glfwWindowHint(GLFW_DECORATED, GL_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
    glfwWindowHint(GLFW_FLOATING, GL_TRUE);
    _window = glfwCreateWindow(RenderTargetWidth*_scale, RenderTargetHeight*_scale, "JPet beta", NULL, NULL);
    glfwSetWindowPos(_window, _iposX, _iposY);
    HWND hwnd = glfwGetWin32Window(_window);
    SetWindowLong(hwnd,GWL_EXSTYLE,WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_ACCEPTFILES);
    // 透明部分鼠标穿透
    SetLayeredWindowAttributes(hwnd,0,0,LWA_COLORKEY);
    if (_window == NULL)
    {
        if (DebugLogEnable)
        {
            LAppPal::PrintLog("Can't create GLFW window.");
        }
        glfwTerminate();
        return GL_FALSE;
    }

    // Windowのコンテキストをカレントに設定
    glfwMakeContextCurrent(_window);
    glfwSwapInterval(1);

    if (glewInit() != GLEW_OK) {
        if (DebugLogEnable)
        {
            LAppPal::PrintLog("Can't initilize glew.");
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
    glfwSetDropCallback(_window,EventHandler::OnDropCallBack);

    // ウィンドウサイズ記憶
    int width, height;
    glfwGetWindowSize(LAppDelegate::GetInstance()->GetWindow(), &width, &height);
    _windowWidth = width;
    _windowHeight = height;

    //AppViewの初期化
    _view->Initialize();

    // Cubism SDK の初期化
    InitializeCubism();

    return GL_TRUE;
}

void LAppDelegate::Release()
{
    // Windowの削除
    glfwDestroyWindow(_window);

    glfwTerminate();

    delete _textureManager;
    delete _view;

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
        if( (_windowWidth!=width || _windowHeight!=height) && width>0 && height>0)
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

        // 画面の初期化
        //glClearColor(1.0f, 1.0f, 1.0f, 0.1f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearDepth(1.0);

        //描画更新
        _view->Render();

        // バッファの入れ替え
        glfwSwapBuffers(_window);

        // Poll for and process events
        glfwPollEvents();
    }
    // Release前保存配置
    int x, y;
    glfwGetWindowPos(_window, &x, &y);
    ofstream of;
    of.open("config.ini", ios::trunc);
    of << "[position]\n" << "x=" << x << "\ny=" << y << "\n[shortcut]\n" << "left=" << _leftUrl << "\nup=" << _upUrl << "\nright=" << _rightUrl<<"\n[display]\n"<<"scale="<<_scale;
    of.close();

    Release();

    LAppDelegate::ReleaseInstance();
}

LAppDelegate::LAppDelegate():
    _cubismOption(),
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
    _leftUrl("https://t.bilibili.com/"),
    _upUrl("https://space.bilibili.com/61639371/dynamic"),
    _rightUrl("https://live.bilibili.com/21484828"),
    _scale(1.0f),
    _windowWidth(0),
    _windowHeight(0)
{
    _view = new LAppView();
    _textureManager = new LAppTextureManager();
}

LAppDelegate::~LAppDelegate()
{

}

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

void LAppDelegate::OnMouseCallBack(GLFWwindow* window, int button, int action, int modify)
{
    if (_view == NULL)
    {
        return;
    }
    if (GLFW_MOUSE_BUTTON_LEFT == button)
    {
        if (GLFW_PRESS == action)
        {
            _captured = true;
            _view->OnTouchesBegan(_mouseX, _mouseY);
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
        if (GLFW_PRESS == action)
        {
            if (DebugLogEnable) LAppPal::PrintLog("Right Click Down");
            _isMsg = true;
            _pX = _mouseX;
            _pY = _mouseY;
        }
        else if (GLFW_RELEASE == action)
        {
            if (DebugLogEnable) LAppPal::PrintLog("Right Click Up");
            float dx = fabs(_mouseX - _pX);
            float dy = fabs(_mouseY - _pY);
            if (dx < 60 && dy < 60);
            else if (_mouseX > _pX && dx > dy ) ShellExecute(NULL, "open", _rightUrl.c_str(), NULL, NULL, SW_SHOWNORMAL); //向右滑动
            else if (_mouseX < _pX && dx > dy ) ShellExecute(NULL, "open", _leftUrl.c_str(), NULL, NULL, SW_SHOWNORMAL); //向左滑动
            else if (_mouseY < _pY && dy > dx ) ShellExecute(NULL, "open", _upUrl.c_str(), NULL, NULL, SW_SHOWNORMAL); //向上滑动
            else if (_mouseY > _pY && dy > dx ) _isEnd = true; //向下滑动
            _isMsg = false;
        }
    }
    return;
}

void LAppDelegate::OnMouseCallBack(GLFWwindow* window, double x, double y)
{
    _mouseX = static_cast<float>(x);
    _mouseY = static_cast<float>(y);

    if (_captured)
    {
        //此时处于Drag状态，移动窗口
        int xpos, ypos;
        glfwGetWindowPos(window, &xpos, &ypos);
        int width,height;
        glfwGetWindowSize(window,&width,&height);
        glfwSetWindowPos(window, xpos + x - width/2, ypos + y - height/2);
        //LAppPal::PrintLog("[APP]Move x:%.2f y:%.2f", x, y);
        return;
    }
    if (_view == NULL)
    {
        return;
    }

    _view->OnTouchesMoved(_mouseX, _mouseY);
}

void LAppDelegate::OnDropCallBack(GLFWwindow* window, int path_count, const char* paths[])
{
    for (int i = 0; i < path_count; i++)
    {
        SHFILEOPSTRUCT shFile;
        ZeroMemory(&shFile,sizeof(shFile));
        shFile.pFrom = paths[i];
        shFile.wFunc = FO_DELETE;
        shFile.fFlags =FOF_SILENT|FOF_ALLOWUNDO|FOF_NOCONFIRMATION;
        SHFileOperation(&shFile);
    }
}

GLuint LAppDelegate::CreateShader()
{
    //バーテックスシェーダのコンパイル
    GLuint vertexShaderId = glCreateShader(GL_VERTEX_SHADER);
    const char* vertexShader =
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
    if(!CheckShader(vertexShaderId))
    {
        return 0;
    }

    //フラグメントシェーダのコンパイル
    GLuint fragmentShaderId = glCreateShader(GL_FRAGMENT_SHADER);
    const char* fragmentShader =
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
        GLchar* log = reinterpret_cast<GLchar*>(CSM_MALLOC(logLength));
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
