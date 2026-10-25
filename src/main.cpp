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
    const wstring dumpfile = LAppDefine::documentPath + L"/minidump_" VERSION ".dmp";
    HANDLE hFile = CreateFile(dumpfile.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile && (hFile != INVALID_HANDLE_VALUE)) {
        MINIDUMP_EXCEPTION_INFORMATION mdei;
        mdei.ThreadId = GetCurrentThreadId();
        mdei.ExceptionPointers = e;
        mdei.ClientPointers = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                          hFile, MiniDumpWithDataSegs, &mdei, NULL, NULL);
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

  // check JPet already running
  HANDLE hMutex = CreateMutex(NULL, FALSE, L"JPetMutex");
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    MessageBox(nullptr, L"JPet 已经在运行中", L"错误", MB_OK);
    return 1;
  }

  INITCOMMONCONTROLSEX icce;
  icce.dwSize = sizeof(INITCOMMONCONTROLSEX);
  icce.dwICC = 0 | ICC_WIN95_CLASSES | ICC_BAR_CLASSES | ICC_COOL_CLASSES;
  InitCommonControlsEx(&icce);
  wchar_t documents[MAX_PATH];
  HRESULT result = SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL,
                                    SHGFP_TYPE_CURRENT, documents);
  LAppDefine::documentPath = wstring(documents) + L"\\jpet";
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
  
  wchar_t curPath[32768];
  const DWORD pathLength = GetModuleFileNameW(nullptr, curPath, 32768);
  if (pathLength == 0 || pathLength >= 32768) {
    LAppPal::PrintLog(LogLevel::Error, "Cannot determine executable directory");
    MessageBox(nullptr, L"无法确定 JPet 安装目录，请重新安装。", L"启动失败", MB_OK | MB_ICONERROR);
    return 1;
  }
  const auto appDirectory = std::filesystem::path(curPath).parent_path();
  // All relative resources must resolve beside the executable, not against
  // the launcher's working directory (shortcuts, startup, or a terminal).
  std::error_code pathError;
  std::filesystem::current_path(appDirectory, pathError);
  if (pathError) {
    // Windows system error messages may use the ANSI code page, not UTF-8.
    LAppPal::PrintLog(LogLevel::Error, "Cannot set resource directory (error %d)", pathError.value());
    MessageBox(nullptr, L"无法访问 JPet 安装目录，请检查目录权限。", L"启动失败", MB_OK | MB_ICONERROR);
    return 1;
  }
  // Callers append "resources/...", so keep a trailing separator.
  LAppDefine::execPath = appDirectory.wstring() + L"\\";

  // check dumpfile
  const wstring dumpfile =
      LAppDefine::documentPath + L"/minidump_" VERSION ".dmp";
  if (std::filesystem::exists(std::filesystem::path(dumpfile))) {
    MessageBox(nullptr,
               L"检测到 JPet "
               L"上次运行时崩溃，请选择目录保存崩溃报告，发送给开发者 @Xinrea",
               L"检测到崩溃报告", MB_OK);
    OPENFILENAME ofn;                         // Common dialog box structure
    wchar_t szFile[260] = L"minidump_" VERSION ".dmp\0";  // Buffer for file name

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"Dump Files\0*.dmp\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    if (GetSaveFileName(&ofn) == TRUE) {
       CopyFile(dumpfile.c_str(), ofn.lpstrFile, FALSE);
       std::filesystem::remove(dumpfile);
    }
  }

  int exitCode = 0;
  try {
    if (LAppDelegate::GetInstance()->Initialize() == GL_FALSE) {
      exitCode = 1;
    } else {
      LAppDelegate::GetInstance()->Run();
    }
  } catch (const std::exception& error) {
    try {
      LAppPal::PrintLog(LogLevel::Error, "JPet startup failed: %s", error.what());
    } catch (...) {
      // what() is not guaranteed to be UTF-8. Logging must not prevent the
      // error dialog or turn a handled startup failure into another crash.
      OutputDebugStringA("JPet startup failed; unable to write the error log.\n");
    }
    MessageBox(nullptr,
               L"JPet 启动失败，资源可能缺失或损坏。请重新安装，并查看文档目录中的 jpet\\jpet.log。",
               L"启动失败", MB_OK | MB_ICONERROR);
    exitCode = 1;
  }

  ReleaseMutex(hMutex);
  CloseHandle(hMutex);
  LAppPal::ReleaseLog();

  return exitCode;
}
