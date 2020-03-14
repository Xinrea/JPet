
#ifndef WINHANDLER_H
#define WINHANDLER_H
#include "wintoastlib.h"

class WinToastEventHandler : public WinToastLib::IWinToastHandler
{
public:
    WinToastEventHandler() = default;
    void toastActivated() const;
    void toastActivated(int actionIndex) const;
    void toastDismissed(WinToastDismissalReason state) const;
    void toastFailed() const;
};

#endif