#include "CookieWindow.hpp"

#include <stdexcept>
#include <thread>

#include "LAppPal.hpp"

LRESULT CALLBACK CookieWndProc(HWND hWnd, UINT message, WPARAM wParam,
                               LPARAM lParam) {
  auto cookieWindow =
      reinterpret_cast<CookieWindow*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
  switch (message) {
    case WM_SIZE:
      if (cookieWindow->webviewController != nullptr) {
        RECT bounds;
        GetClientRect(hWnd, &bounds);
        cookieWindow->webviewController->put_Bounds(bounds);
      };
      break;
    case WM_DESTROY:
      // just hide window
      ShowWindow(hWnd, SW_HIDE);
      break;
    default:
      return DefWindowProc(hWnd, message, wParam, lParam);
      break;
  }

  return 0;
}

CookieWindow::CookieWindow(HWND parent, HINSTANCE instance)
    : _parent(parent), _instance(instance), _visible(false) {
  // thread for Window
  LAppPal::PrintLog("[GamePanel]Parent: %p, Instance: %p", parent, instance);
  WindowProc();
}
void CookieWindow::WindowProc() {
  WNDCLASSEX wcex;

  wcex.cbSize = sizeof(WNDCLASSEX);
  wcex.style = CS_HREDRAW | CS_VREDRAW;
  wcex.lpfnWndProc = CookieWndProc;
  wcex.cbClsExtra = 0;
  wcex.cbWndExtra = 0;
  wcex.hInstance = _instance;
  wcex.hIcon = LoadIcon(_instance, IDI_APPLICATION);
  wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wcex.lpszMenuName = NULL;
  wcex.lpszClassName = L"CookieWindow";
  wcex.hIconSm = LoadIcon(wcex.hInstance, IDI_APPLICATION);

  if (!RegisterClassEx(&wcex)) {
    LAppPal::PrintLog("Failed to register window class");
    return;
  }

  _window = CreateWindow(L"CookieWindow", L"CookieWindow", WS_OVERLAPPEDWINDOW,
                         CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, NULL, NULL,
                         _instance, NULL);

  if (!_window) {
    LAppPal::PrintLog("Failed to create window");
    return;
  }

  LAppPal::PrintLog("CookieWindow: %p", _window);

  SetWindowLongPtr(_window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

  ShowWindow(_window, SW_HIDE);
  UpdateWindow(_window);
  // <-- WebView2 sample code starts here -->
  // Step 3 - Create a single WebView within the parent window
  // Locate the browser and set up the environment for WebView
  CreateCoreWebView2EnvironmentWithOptions(
      nullptr, nullptr, nullptr,
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

                      EventRegistrationToken token;
                      webview->add_WebMessageReceived(
                          Microsoft::WRL::Callback<
                              ICoreWebView2WebMessageReceivedEventHandler>(
                              [this](ICoreWebView2* webview,
                                     ICoreWebView2WebMessageReceivedEventArgs*
                                         args) -> HRESULT {
                                wil::unique_cotaskmem_string message;
                                args->TryGetWebMessageAsString(&message);
                                // processMessage(&message);
                                std::wstring wstr(message.get());
                                std::string str(wstr.begin(), wstr.end());
                                if (str[0] == 'C') {
                                  cookie = str.substr(1);
                                } else if (str[0] == 'U') {
                                  userAgent = str.substr(1);
                                }
                                if (_visible) {
                                  ShowWindow(_window, SW_HIDE);
                                  _visible = false;
                                }
                                return S_OK;
                              })
                              .Get(),
                          &token);
                      webview->add_NavigationCompleted(
                          Microsoft::WRL::Callback<
                              ICoreWebView2NavigationCompletedEventHandler>(
                              [](ICoreWebView2* sender,
                                 ICoreWebView2NavigationCompletedEventArgs*
                                     args) {
                                BOOL isSuccess;
                                args->get_IsSuccess(&isSuccess);
                                if (isSuccess) {
                                  sender->ExecuteScript(
                                      L"window.chrome.webview.postMessage('C'+"
                                      L"window."
                                      L"document.cookie);"
                                      L"window.chrome.webview.postMessage('U'+"
                                      L"window.navigator.userAgent);",
                                      nullptr);
                                }
                                return S_OK;
                              })
                              .Get(),
                          &token);
                      webview->Navigate(
                          L"https://space.bilibili.com/61639371/dynamic");

                      return S_OK;
                    })
                    .Get());
            return S_OK;
          })
          .Get());
}

void CookieWindow::Show() {
  if (_window) {
    ShowWindow(_window, SW_SHOW);
    _visible = true;
  }
}

void CookieWindow::UpdateCookie() {
  if (webview == nullptr) {
    LAppPal::PrintLog("[CookieWindow]Webview is null");
    return;
  }
  LAppPal::PrintLog("[CookieWindow]Try update cookies");
  webview->ExecuteScript(
      L"window.chrome.webview.postMessage('C'+window.document.cookie)",
      nullptr);
}

CookieWindow::~CookieWindow() {
  if (_window) {
    DestroyWindow(_window);
  }
}
