#include "Platform.hpp"

#include <windows.h>
#include <shellapi.h>

#include "LAppPal.hpp"
#include <fstream>
#include <vector>

void Platform::Open(const std::string& pathOrURL) {
  const auto path = LAppPal::StringToWString(pathOrURL);
  ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool Platform::OpenWebURL(const std::string& url, std::string& error) {
  const auto address = LAppPal::StringToWString(url);
  const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", address.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  if (result <= 32) error = "无法启动系统默认浏览器，请检查默认浏览器设置";
  return result > 32;
}

namespace {
std::filesystem::path InstallPath() {
  std::vector<wchar_t> path(32768);
  const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (!length || length >= path.size()) throw std::runtime_error("Cannot locate JPet.exe");
  return std::filesystem::path(std::wstring(path.data(), length)).parent_path();
}
std::wstring QuotePS(const std::filesystem::path& path) {
  std::wstring result = L"'";
  for (auto c : path.native()) { result += c; if (c == L'\'') result += L'\''; }
  return result + L"'";
}
std::wstring Powershell() {
  wchar_t system[MAX_PATH];
  if (!GetSystemDirectoryW(system, MAX_PATH)) throw std::runtime_error("Cannot locate PowerShell");
  return std::wstring(system) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
}
}

bool Platform::ExtractUpdate(const std::filesystem::path& archive,
                             const std::filesystem::path& stage, std::string& error) {
  try {
    std::wstring command = L"\"" + Powershell() + L"\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"$ErrorActionPreference='Stop'; Expand-Archive -LiteralPath " +
        QuotePS(archive) + L" -DestinationPath " + QuotePS(stage) + L" -Force\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
      error = "无法启动更新包解压工具"; return false;
    }
    const auto wait = WaitForSingleObject(process.hProcess, 120000);
    DWORD code = 1;
    if (wait == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &code);
    else TerminateProcess(process.hProcess, 1);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    if (code != 0) { error = "更新包解压失败，请重新下载"; return false; }
    return true;
  } catch (const std::exception&) { error = "无法启动更新包解压工具"; return false; }
}

bool Platform::LaunchUpdate(const std::filesystem::path& staged,
                            const std::filesystem::path& work,
                            const std::filesystem::path& failureLog, std::string& error) {
  try {
    const auto target = InstallPath();
    const auto script = work / L"updater.ps1";
    std::filesystem::copy_file(target / L"resources/updater/windows.ps1", script, std::filesystem::copy_options::overwrite_existing);
    const auto probe = target.parent_path() / (L".jpet-write-test-" + work.filename().native());
    HANDLE test = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    const bool elevate = test == INVALID_HANDLE_VALUE;
    if (!elevate) CloseHandle(test);
    const auto quote = [](const std::filesystem::path& value) { return L"\"" + value.native() + L"\""; };
    const std::wstring parameters = L"-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " + quote(script) +
        L" -ParentId " + std::to_wstring(GetCurrentProcessId()) + L" -Staged " + quote(staged) +
        L" -Target " + quote(target) + L" -FailureLog " + quote(failureLog);
    const auto powershell = Powershell();
    SHELLEXECUTEINFOW launch{}; launch.cbSize = sizeof(launch); launch.fMask = SEE_MASK_NOCLOSEPROCESS;
    launch.lpVerb = elevate ? L"runas" : L"open"; launch.lpFile = powershell.c_str();
    launch.lpParameters = parameters.c_str(); launch.nShow = SW_HIDE;
    if (!ShellExecuteExW(&launch)) { error = "无法启动更新助手，或更新授权已取消"; return false; }
    if (launch.hProcess) CloseHandle(launch.hProcess);
    return true;
  } catch (const std::exception&) { error = "无法准备更新助手，请检查磁盘空间"; return false; }
}
