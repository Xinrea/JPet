/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <string>
#include "AudioManager.hpp"
#include "LAppAllocator.hpp"

class LAppView;
class LAppTextureManager;

/**
* @brief   アプリケーションクラス。
*   Cubism SDK の管理を行う。
*/
class LAppDelegate
{
public:
    /**
    * @brief   クラスのインスタンス（シングルトン）を返す。<br>
    *           インスタンスが生成されていない場合は内部でインスタンを生成する。
    *
    * @return  クラスのインスタンス
    */
    static LAppDelegate* GetInstance();

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
    void OnMouseCallBack(GLFWwindow* window, int button, int action, int modify);

    /**
    * @brief   OpenGL用 glfwSetCursorPosCallback用関数。
    *
    * @param[in]       window            コールバックを呼んだWindow情報
    * @param[in]       x                 x座標
    * @param[in]       y                 x座標
    */
    void OnMouseCallBack(GLFWwindow* window, double x, double y);

    void OnDropCallBack(GLFWwindow* window, int path_count, const char* paths[]);

    void OnWindowPosCallBack(GLFWwindow* window, int x, int y);

    /**
    * @brief シェーダーを登録する。
    */
    GLuint CreateShader();

    /**
    * @brief   Window情報を取得する。
    */
    GLFWwindow* GetWindow() { return _window; }

    /**
    * @brief   View情報を取得する。
    */
    LAppView* GetView() { return _view; }

    /**
    * @brief   アプリケーションを終了するかどうか。
    */
    bool GetIsEnd() { return _isEnd; }

    bool GetIsMsg() { return _isMsg; }
    /**
    * @brief   アプリケーションを終了する。
    */
    void AppEnd() { _isEnd = true; }

    LAppTextureManager* GetTextureManager() { return _textureManager; }

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

    LAppAllocator _cubismAllocator;              ///< Cubism SDK Allocator
    Csm::CubismFramework::Option _cubismOption;  ///< Cubism SDK Option
    GLFWwindow* _window;                         ///< OpenGL ウィンドウ
    LAppView* _view;                             ///< View情報
    bool _captured;                              ///< クリックしているか
    float _mouseX;                               ///< マウスX座標
    float _mouseY;                               ///< マウスY座標
    float _pX;
    float _pY;
    double _cX, _cY;
    bool _isEnd;                                 ///< APP終了しているか
    bool _isMsg;
    int _mWidth, _mHeight;
    AudioManager* _au;
    LAppTextureManager* _textureManager;         ///< テクスチャマネージャー

    // Config Part
    int _iposX,_iposY;
    std::string _leftUrl,_upUrl,_rightUrl;
    bool _isSetting;
    float _timeSetting;
    double _holdTime;
    int _volume;
    bool _mute;
    
    int _windowWidth;                            ///< Initialize関数で設定したウィンドウ幅
    int _windowHeight;                           ///< Initialize関数で設定したウィンドウ高さ
};

class EventHandler
{
public:
    /**
    * @brief   glfwSetMouseButtonCallback用コールバック関数。
    */
    static void OnMouseCallBack(GLFWwindow* window, int button, int action, int modify)
    {
        LAppDelegate::GetInstance()->OnMouseCallBack(window, button, action, modify);
    }

    /**
    * @brief   glfwSetCursorPosCallback用コールバック関数。
    */
    static void OnMouseCallBack(GLFWwindow* window, double x, double y)
    {
         LAppDelegate::GetInstance()->OnMouseCallBack(window, x, y);
    }

    static void OnDropCallBack(GLFWwindow* window, int path_count, const char* paths[])
    {
         LAppDelegate::GetInstance()->OnDropCallBack(window, path_count, paths);
    }

    static void OnWindowPosCallBack(GLFWwindow* window, int x, int y)
    {
        LAppDelegate::GetInstance()->OnWindowPosCallBack(window, x,  y);
    }

};
