/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at
 * https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppDefine.hpp"
#include "LAppDelegate.hpp"

#include <minidumpapiset.h>

#pragma comment(linker, "/entry:\"mainCRTStartup\"")
#pragma comment( \
    linker,      \
    "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "comctl32.lib")

LONG WINAPI unhandled_handler(EXCEPTION_POINTERS* e) {
    const wstring dumpfile = LAppDefine::documentPath + L"\\minidump.dmp";
    HANDLE hFile = CreateFile(dumpfile.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile && (hFile != INVALID_HANDLE_VALUE)) {
        MINIDUMP_EXCEPTION_INFORMATION mdei;
        mdei.ThreadId = GetCurrentThreadId();
        mdei.ExceptionPointers = e;
        mdei.ClientPointers = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                          hFile, MiniDumpNormal, &mdei, NULL, NULL);
        CloseHandle(hFile);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

int main() {
  SetUnhandledExceptionFilter(unhandled_handler);
  INITCOMMONCONTROLSEX icce;
  icce.dwSize = sizeof(INITCOMMONCONTROLSEX);
  icce.dwICC = 0 | ICC_WIN95_CLASSES | ICC_BAR_CLASSES | ICC_COOL_CLASSES;
  InitCommonControlsEx(&icce);
  wchar_t documents[MAX_PATH];
  HRESULT result = SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL,
                                    SHGFP_TYPE_CURRENT, documents);
  LAppDefine::documentPath = wstring(documents) + L"\\jpet\\";
  if (!PathFileExists(LAppDefine::documentPath.c_str())) {
    CreateDirectory(LAppDefine::documentPath.c_str(), NULL);
  }
  wchar_t curPath[256];
  GetModuleFileName(GetModuleHandle(NULL), static_cast<LPWSTR>(curPath),
                    sizeof(curPath));
  LAppDefine::execPath = std::wstring(curPath);
  // create the application instance
  if (LAppDelegate::GetInstance()->Initialize() == GL_FALSE) {
    return 1;
  }
  LAppDelegate::GetInstance()->Run();

  return 0;
}
