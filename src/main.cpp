/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at
 * https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppDefine.hpp"
#include "LAppDelegate.hpp"

#include <commdlg.h>
#include <minidumpapiset.h>
#include <filesystem>

#pragma comment(linker, "/entry:\"mainCRTStartup\"")
#pragma comment( \
    linker,      \
    "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "comctl32.lib")

LONG WINAPI unhandled_handler(EXCEPTION_POINTERS* e) {
    const wstring dumpfile = LAppDefine::documentPath + L"/minidump.dmp";
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

bool CheckWebView2Runtime() {
    LPWSTR versionInfo;
    HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &versionInfo);
    if (hr == S_OK && versionInfo != nullptr) {
        LAppPal::PrintLog(LogLevel::Info, L"WebView2 Runtime is installed. Version: %ls", versionInfo);
        CoTaskMemFree(versionInfo);
        return true;
    } else {
        LAppPal::PrintLog(LogLevel::Error, L"WebView2 Runtime is not installed.");
        return false;
    }
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

  LAppPal::Init();

  // check WebView2 runtime
  if (!CheckWebView2Runtime()) {
    MessageBox(nullptr, L"请安装 WebView2 Runtime 后再运行 JPet", L"错误",
               MB_OK);
    ShellExecute(nullptr, L"open",
                 L"https://go.microsoft.com/fwlink/p/?LinkId=2124703",
                 nullptr, nullptr, SW_SHOWNORMAL);
    return 1;
  }
  
  wchar_t curPath[256];
  GetModuleFileName(GetModuleHandle(NULL), static_cast<LPWSTR>(curPath),
                    sizeof(curPath));
  LAppDefine::execPath = std::wstring(curPath);

  // check dumpfile
  const wstring filepath = LAppDefine::documentPath + L"/minidump.dmp";
  if (std::filesystem::exists(std::filesystem::path(filepath))) {
    MessageBox(nullptr,
               L"检测到 JPet "
               L"上次运行时崩溃，请选择目录保存崩溃报告，发送给开发者 @Xinrea",
               L"检测到崩溃报告", MB_OK);
    OPENFILENAME ofn;                         // Common dialog box structure
    wchar_t szFile[260] = L"minidump.dmp\0";  // Buffer for file name

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = L"Dump Files (*.dmp)\0*.dmp\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetSaveFileName(&ofn) == TRUE) {
      // Use ofn.lpstrFile here to open the file for writing
      CopyFile(filepath.c_str(), ofn.lpstrFile, TRUE);
      DeleteFile(filepath.c_str());
    }
  }

  // create the application instance
  if (LAppDelegate::GetInstance()->Initialize() == GL_FALSE) {
    return 1;
  }
  LAppDelegate::GetInstance()->Run();

  return 0;
}
