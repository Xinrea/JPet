#include "WinToastEventHandler.h"

#include <Windows.h>
#include <shellapi.h>

#include "LAppPal.hpp"
#include "LAppDelegate.hpp"
#include "PanelServer.hpp"

WinToastEventHandler::WinToastEventHandler(std::string u) { url = u; }

void WinToastEventHandler::toastActivated() const {
  LAppPal::PrintLog("[WinToastEventHandler]ToastActivated");
  // convert string to wstring
  if (url.empty()) {
    return;
  }
  if (url == "TASK_COMPLETE") {
    LAppDelegate::GetInstance()->ShowPanel();
    PanelServer::GetInstance()->Notify("TASK_COMPLETE");
    return;
  }
  std::wstring url = std::wstring(this->url.begin(), this->url.end());
  ShellExecute(NULL, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

void WinToastEventHandler::toastActivated(int actionIndex) const {}

void WinToastEventHandler::toastDismissed(WinToastDismissalReason state) const {
}

void WinToastEventHandler::toastFailed() const {}
