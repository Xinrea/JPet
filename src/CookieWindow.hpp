#pragma once

#ifndef __APPLE__
#include <wrl.h>
#include <wil/com.h>
#include <webview2.h>
#endif

#include <memory>
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
#ifdef __APPLE__
  CookieWindow(void* parent = nullptr);
#else
  CookieWindow(HWND parent, HINSTANCE instance);
#endif

  // Read snapshots rather than sharing mutable browser state with the watcher.
  std::string GetUserAgent() const;
  std::string GetCookies() const;

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

#ifdef __APPLE__
 private:
  struct Impl;
  std::shared_ptr<Impl> _impl;
#else
  wil::com_ptr<ICoreWebView2Controller> webviewController;

 private:
  HWND _parent;
  HWND _window;
  HINSTANCE _instance;
  bool _visible;

  wil::com_ptr<ICoreWebView2> webview;
  wil::com_ptr<ICoreWebView2CookieManager> _cookieManager;

  void WindowProc();
#endif
};

#ifndef __APPLE__
inline std::string CookieWindow::GetUserAgent() const { return userAgent; }
inline std::string CookieWindow::GetCookies() const { return cookie; }
#endif
