#pragma once

#include <string>

// System integration; UI work is marshalled to the AppKit main thread on macOS.
namespace Platform {
void Open(const std::string& pathOrURL);

#ifdef __APPLE__
void Alert(const std::wstring& title, const std::wstring& message);
void Notify(const std::wstring& title, const std::wstring& message,
            const std::string& url = "");
bool SaveFile(const std::wstring& source, const std::wstring& suggestedName);
bool TrashFile(const std::wstring& path);
bool Browse(std::wstring& path, bool directory);
#endif
}
