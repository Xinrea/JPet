#include "GamePanel.hpp"
#include "LAppDefine.hpp"
#include "LAppPal.hpp"
#include "resource.h"

#include <winuser.h>

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam,
                         LPARAM lParam) {
  auto gamePanel =
      reinterpret_cast<GamePanel*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
  switch (message) {
    case WM_SIZE:
      if (gamePanel->webviewController != nullptr) {
        RECT bounds;
        GetClientRect(hWnd, &bounds);
        gamePanel->webviewController->put_Bounds(bounds);
      };
      break;
    case WM_CLOSE:
      // just hide window
      ShowWindow(hWnd, SW_HIDE);
      break;
    default:
      return DefWindowProc(hWnd, message, wParam, lParam);
      break;
  }

  return 0;
}

GamePanel::GamePanel(HWND parent, HINSTANCE instance)
    : _parent(parent), _instance(instance) {
  // thread for Window
  // print parent and instance
  LAppPal::PrintLog(LogLevel::Debug, "[GamePanel]Parent: %p, Instance: %p",
                    parent, instance);
  WindowProc();
}
void GamePanel::WindowProc() {
  WNDCLASSEX wcex;

  wcex.cbSize = sizeof(WNDCLASSEX);
  wcex.style = CS_HREDRAW | CS_VREDRAW;
  wcex.lpfnWndProc = WndProc;
  wcex.cbClsExtra = 0;
  wcex.cbWndExtra = 0;
  wcex.hInstance = _instance;
  wcex.hIcon = LoadIcon(_instance, MAKEINTRESOURCE(IDI_ICON1));
  wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wcex.lpszMenuName = NULL;
  wcex.lpszClassName = L"GamePanel";
  wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_ICON1));

  if (!RegisterClassEx(&wcex)) {
    LAppPal::PrintLog("Failed to register window class");
    return;
  }

  _window = CreateWindow(L"GamePanel", L"设置面板", WS_OVERLAPPED | WS_CAPTION | WS_MINIMIZEBOX | WS_SYSMENU,
                         CW_USEDEFAULT, CW_USEDEFAULT, 800, 1300, NULL, NULL,
                         _instance, NULL);

  if (!_window) {
    LAppPal::PrintLog("Failed to create window");
    return;
  }

  LAppPal::PrintLog(LogLevel::Debug, "[GamePanel]Window: %p", _window);

  SetWindowPos(_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

  SetWindowLongPtr(_window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

  ShowWindow(_window, SW_HIDE);
  UpdateWindow(_window);
  // <-- WebView2 sample code starts here -->
  // Step 3 - Create a single WebView within the parent window
  // Locate the browser and set up the environment for WebView
  try {
    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, (LAppDefine::documentPath + L"\\GamePanelData").c_str(), nullptr,
        Microsoft::WRL::Callback<
            ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
              // Create a CoreWebView2Controller and get the associated
              // CoreWebView2 whose parent is the main window hWnd
              env->CreateCoreWebView2Controller(
                  _window,
                  Microsoft::WRL::Callback<
                      ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                      [this](HRESULT result,
                             ICoreWebView2Controller* controller) -> HRESULT {
                        if (controller != nullptr) {
                          webviewController = controller;
                          webviewController->get_CoreWebView2(&webview);
                        }

                        // Add a few settings for the webview
                        // The demo step is redundant since the values are the
                        // default settings
                        wil::com_ptr<ICoreWebView2Settings> settings;
                        webview->get_Settings(&settings);
                        settings->put_IsScriptEnabled(TRUE);
                        settings->put_AreDefaultScriptDialogsEnabled(TRUE);
                        settings->put_IsWebMessageEnabled(TRUE);

                        // Resize WebView to fit the bounds of the parent window
                        RECT bounds;
                        GetClientRect(_window, &bounds);
                        webviewController->put_Bounds(bounds);

                        // webview->Navigate(L"https://www.bilibili.com/");
                        webview->Navigate(L"http://localhost:8053/index.html");

                        // fix webview not render in hidden-created window
                        webviewController->put_IsVisible(true);

                        return S_OK;
                      })
                      .Get());
              return S_OK;
            })
            .Get());
  } catch (std::exception& e) {
    LAppPal::PrintLog(LogLevel::Error, "Failed to create WebView: %s",
                      e.what());
  }
}

GamePanel::~GamePanel() {
  if (_window) {
    DestroyWindow(_window);
  }
}
