/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppDelegate.hpp"
#include "LAppDefine.hpp"

#pragma comment(linker, "/entry:\"mainCRTStartup\"")
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "comctl32.lib" )

int main()
{
    INITCOMMONCONTROLSEX icce;
    icce.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icce.dwICC = 0
        | ICC_WIN95_CLASSES
        | ICC_BAR_CLASSES
        | ICC_COOL_CLASSES
        ;
    InitCommonControlsEx(&icce);
    CHAR documents[MAX_PATH];
    HRESULT result = SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, documents);
    LAppDefine::documentPath = string(documents);
    // create the application instance
    if (LAppDelegate::GetInstance()->Initialize() == GL_FALSE)
    {
        return 1;
    }

    LAppDelegate::GetInstance()->Run();

    return 0;
}

