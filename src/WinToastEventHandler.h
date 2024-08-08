#pragma once

#include <string>

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
