#pragma once

#include <string>
#include <filesystem>

// System integration; UI work is marshalled to the AppKit main thread on macOS.
namespace Platform {
void Open(const std::string& pathOrURL);
// Only for validated HTTP(S) URLs; reports whether the system accepted launch.
bool OpenWebURL(const std::string& url, std::string& error);
bool ExtractUpdate(const std::filesystem::path& archive,
                   const std::filesystem::path& stage, std::string& error);
bool LaunchUpdate(const std::filesystem::path& staged,
                  const std::filesystem::path& work,
                  const std::filesystem::path& failureLog, std::string& error);

#ifdef __APPLE__
void Alert(const std::wstring& title, const std::wstring& message);
void Notify(const std::wstring& title, const std::wstring& message,
            const std::string& url = "");
bool SaveFile(const std::wstring& source, const std::wstring& suggestedName);
bool TrashFile(const std::wstring& path);
bool Browse(std::wstring& path, bool directory);
#endif
}
