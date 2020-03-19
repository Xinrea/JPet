
#ifndef WINHANDLER_H
#define WINHANDLER_H
#include "wintoastlib.h"
#include <string>

class WinToastEventHandler : public WinToastLib::IWinToastHandler
{
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