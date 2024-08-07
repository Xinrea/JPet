#pragma once

#include <wrl.h>
#include <wil/com.h>
#include <webview2.h>

#include <string>

/**
 * @brief A simple wrapper for webview of cookie window for bypass bilibili
 * check
 */
class CookieWindow {
 public:
  std::string userAgent;
  std::string cookie;
  /**
   * @brief Construct a new GamePanel object
   *
   * @param[in] parent The parent window handle
   */
  CookieWindow(HWND parent, HINSTANCE instance);

  /**
   * @brief Destroy the GamePanel object
   */
  ~CookieWindow();

  /**
   * @brief Navigate to the given URL
   *
   * @param[in] url The URL to navigate to
   */
  void Navigate(const std::string& url);

  void Reload();

  // Only use in message proc
  void doReload();

  /**
   * @brief Resize the panel
   *
   * @param[in] width The new width
   * @param[in] height The new height
   */
  void Resize(int width, int height);

  /**
   * @brief Show the panel
   */
  void Show();

  /**
   * @brief Hide the panel
   */
  void Hide();

  /**
   * @brief Check if the panel is visible
   *
   * @return true if the panel is visible
   */
  bool IsVisible();

  void UpdateCookie();

  wil::com_ptr<ICoreWebView2Controller> webviewController;

 private:
  HWND _parent;
  HWND _window;
  HINSTANCE _instance;
  bool _visible;

  wil::com_ptr<ICoreWebView2> webview;
  wil::com_ptr<ICoreWebView2CookieManager> _cookieManager;

  void WindowProc();
};
