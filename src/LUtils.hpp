#pragma once
#include <windows.h>

#include <string>

class LUtils {
 public:
  static void OpenURL(const std::string& url) {
    // convert string to wstring
    std::wstring wurl = std::wstring(url.begin(), url.end());
    ShellExecute(NULL, L"open", wurl.c_str(), NULL, NULL,
                 SW_SHOWNORMAL);  // Use ShellExecuteW instead of ShellExecute
  }
};