#include "Platform.hpp"

#include <windows.h>
#include <shellapi.h>

#include "LAppPal.hpp"

void Platform::Open(const std::string& pathOrURL) {
  const auto path = LAppPal::StringToWString(pathOrURL);
  ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
