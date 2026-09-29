#pragma once

#include <string>
#include <utility>

#ifdef __APPLE__
// Keep the existing notification call interface without importing WinToast.
class WinToastEventHandler {
 public:
  explicit WinToastEventHandler(std::string url) : _url(std::move(url)) {}
  const std::string& GetUrl() const { return _url; }
 private:
  std::string _url;
};
#else
#include <wintoastlib.h>

// WinToastEventHandler only handles the toast events to open none-unicode URLs
class WinToastEventHandler : public WinToastLib::IWinToastHandler {
 private:
  std::string url;

 public:
  WinToastEventHandler(std::string u);
  void toastActivated() const;
  void toastActivated(int actionIndex) const;
  void toastDismissed(WinToastDismissalReason state) const;
  void toastFailed() const;
};
#endif
