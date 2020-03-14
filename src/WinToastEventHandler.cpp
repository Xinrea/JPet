#include "WinToastEventHandler.h"

void WinToastEventHandler::toastActivated() const
{
    ShellExecute(NULL, "open", "https://live.bilibili.com/21484828", NULL, NULL, SW_SHOWNORMAL);
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