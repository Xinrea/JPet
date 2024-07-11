#include "GamePanel.hpp"

#include <stdexcept>
#include <thread>

#include "LAppPal.hpp"

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

GamePanel::GamePanel(HWND parent, HINSTANCE instance)
    : _parent(parent), _instance(instance), _visible(false) {
  // thread for Window
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
  wcex.hIcon = LoadIcon(_instance, IDI_APPLICATION);
  wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wcex.lpszMenuName = NULL;
  wcex.lpszClassName = L"GamePanel";
  wcex.hIconSm = LoadIcon(wcex.hInstance, IDI_APPLICATION);

  if (!RegisterClassEx(&wcex)) {
    LAppPal::PrintLog("Failed to register window class");
    return;
  }

  _window = CreateWindow(L"GamePanel", L"GamePanel", WS_OVERLAPPEDWINDOW,
                         CW_USEDEFAULT, CW_USEDEFAULT, 1200, 900, NULL, NULL,
                         _instance, NULL);

  if (!_window) {
    LAppPal::PrintLog("Failed to create window");
    return;
  }

  SetWindowLongPtr(_window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

  ShowWindowAsync(_window, SW_SHOWDEFAULT);
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

                      // Schedule an async task to navigate to Bing
                      webview->Navigate(L"https://www.bing.com/");

                      // <NavigationEvents>
                      // Step 4 - Navigation events
                      // register an ICoreWebView2NavigationStartingEventHandler
                      // to cancel any non-https navigation
                      EventRegistrationToken token;
                      webview->add_NavigationStarting(
                          Microsoft::WRL::Callback<
                              ICoreWebView2NavigationStartingEventHandler>(
                              [](ICoreWebView2* webview,
                                 ICoreWebView2NavigationStartingEventArgs* args)
                                  -> HRESULT {
                                wil::unique_cotaskmem_string uri;
                                args->get_Uri(&uri);
                                std::wstring source(uri.get());
                                if (source.substr(0, 5) != L"https") {
                                  args->put_Cancel(true);
                                }
                                return S_OK;
                              })
                              .Get(),
                          &token);
                      // </NavigationEvents>

                      // <Scripting>
                      // Step 5 - Scripting
                      // Schedule an async task to add initialization script
                      // that freezes the Object object
                      webview->AddScriptToExecuteOnDocumentCreated(
                          L"Object.freeze(Object);", nullptr);
                      // Schedule an async task to get the document URL
                      webview->ExecuteScript(
                          L"window.document.URL;",
                          Microsoft::WRL::Callback<
                              ICoreWebView2ExecuteScriptCompletedHandler>(
                              [](HRESULT errorCode,
                                 LPCWSTR resultObjectAsJson) -> HRESULT {
                                LPCWSTR URL = resultObjectAsJson;
                                return S_OK;
                              })
                              .Get());
                      // </Scripting>

                      // <CommunicationHostWeb>
                      // Step 6 - Communication between host and web content
                      // Set an event handler for the host to return received
                      // message back to the web content
                      webview->add_WebMessageReceived(
                          Microsoft::WRL::Callback<
                              ICoreWebView2WebMessageReceivedEventHandler>(
                              [](ICoreWebView2* webview,
                                 ICoreWebView2WebMessageReceivedEventArgs* args)
                                  -> HRESULT {
                                wil::unique_cotaskmem_string message;
                                args->TryGetWebMessageAsString(&message);
                                // processMessage(&message);
                                webview->PostWebMessageAsString(message.get());
                                return S_OK;
                              })
                              .Get(),
                          &token);

                      // Schedule an async task to add initialization script
                      // that 1) Add an listener to print message from the host
                      // 2) Post document URL to the host
                      webview->AddScriptToExecuteOnDocumentCreated(
                          L"window.chrome.webview.addEventListener(\'message\',"
                          L" event => alert(event.data));"
                          L"window.chrome.webview.postMessage(window.document."
                          L"URL);",
                          nullptr);
                      // </CommunicationHostWeb>

                      return S_OK;
                    })
                    .Get());
            return S_OK;
          })
          .Get());
}
