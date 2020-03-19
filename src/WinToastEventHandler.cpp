#include "WinToastEventHandler.h"

WinToastEventHandler::WinToastEventHandler(std::string u)
{
    url = u;
}

void WinToastEventHandler::toastActivated() const
{
    ShellExecute(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

void WinToastEventHandler::toastActivated(int actionIndex) const
{
}

void WinToastEventHandler::toastDismissed(WinToastDismissalReason state) const
{
}

void WinToastEventHandler::toastFailed() const
{
}