/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at
 * https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppPal.hpp"

#include <GLFW/glfw3.h>
#include <sys/stat.h>

#include <Model/CubismMoc.hpp>
#include <cstdarg>
#include <iostream>
#include <codecvt>
#include <locale>
#include <windows.h>

#include "LAppDefine.hpp"

using namespace Csm;
using namespace std;
using namespace LAppDefine;

double LAppPal::s_currentFrame = 0.0;
double LAppPal::s_lastFrame = 0.0;
double LAppPal::s_deltaTime = 0.0;
std::wfstream LAppPal::s_logFile(documentPath + L"/jpet.log",
                                std::ios::out | std::ios::app);

csmByte* LAppPal::LoadFileAsBytes(const string& filePath, csmSizeInt* outSize) {
  // filePath;//
  const char* path = filePath.c_str();

  int size = 0;
  struct stat statBuf {};
  if (stat(path, &statBuf) == 0) {
    size = statBuf.st_size;
  }

  std::fstream file;
  char* buf = new char[size];

  file.open(path, std::ios::in | std::ios::binary);
  if (!file.is_open()) {
    if (DebugLogEnable) {
      PrintLog("file open error: %s", filePath.c_str());
    }
    return nullptr;
  }
  file.read(buf, size);
  file.close();

  *outSize = size;
  return reinterpret_cast<csmByte*>(buf);
}

void LAppPal::ReleaseBytes(const csmByte* byteData) { delete[] byteData; }

csmFloat32 LAppPal::GetDeltaTime() {
  return static_cast<csmFloat32>(s_deltaTime);
}

void LAppPal::UpdateTime() {
  s_currentFrame = glfwGetTime();
  s_deltaTime = s_currentFrame - s_lastFrame;
  s_lastFrame = s_currentFrame;
}

void LAppPal::PrintLog(const csmChar* format, ...) {
  va_list args;
  csmChar buf[4096];
  va_start(args, format);
  vsnprintf_s(buf, sizeof(buf), format, args);  // 標準出力でレンダリング
#ifdef CSM_DEBUG_MEMORY_LEAKING
  // メモリリークチェック時は大量の標準出力がはしり重いのでprintfを利用する
  std::printf(buf);
#else
  // add time info
  time_t now = time(nullptr);
  struct tm* pnow = localtime(&now);
  char timebuf[32];
  strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", pnow);
  wstring output = StringToWString(string(timebuf) + "[INFO]" + buf);
  s_logFile << output << std::endl;
  std::wcerr << output << std::endl;
#endif
  va_end(args);
}

void LAppPal::PrintLog(LogLevel level, const wchar_t* format, ...) {
  if (!DebugLogEnable && level == LogLevel::Debug) {
    return;
  }
  va_list args;
  wchar_t buf[4096];
  va_start(args, format);
  vswprintf_s(buf, sizeof(buf) / sizeof(wchar_t), format, args);  // 標準出力でレンダリング
  // add time info
  time_t now = time(nullptr);
  struct tm* pnow = localtime(&now);
  wchar_t timebuf[64];
  wcsftime(timebuf, sizeof(timebuf), L"[%Y-%m-%d %H:%M:%S]", pnow);
  wstring levelStr = L"[INFO]";
  if (level == LogLevel::Debug) levelStr = L"[DEBUG]";
  if (level == LogLevel::Info) levelStr = L"[INFO]";
  if (level == LogLevel::Warn) levelStr = L"[WARN]";
  if (level == LogLevel::Error) levelStr = L"[ERROR]";
  s_logFile << timebuf << levelStr << buf << std::endl;
  std::wcerr << timebuf << levelStr << buf << std::endl;
  va_end(args);
}

void LAppPal::PrintLog(LogLevel level, const csmChar* format, ...) {
  if (!DebugLogEnable && level == LogLevel::Debug) {
    return;
  }
  va_list args;
  csmChar buf[4096];
  va_start(args, format);
  vsnprintf_s(buf, sizeof(buf), format, args);  // 標準出力でレンダリング
#ifdef CSM_DEBUG_MEMORY_LEAKING
  // メモリリークチェック時は大量の標準出力がはしり重いのでprintfを利用する
  std::printf(buf);
#else
  // add time info
  time_t now = time(nullptr);
  struct tm* pnow = localtime(&now);
  char timebuf[32];
  strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S]", pnow);
  string levelStr = "[INFO]";
  if (level == LogLevel::Debug) levelStr = "[DEBUG]";
  if (level == LogLevel::Info) levelStr = "[INFO]";
  if (level == LogLevel::Warn) levelStr = "[WARN]";
  if (level == LogLevel::Error) levelStr = "[ERROR]";
  wstring output = StringToWString(timebuf + levelStr + buf);
  s_logFile << output << std::endl;
  std::wcerr << output << std::endl;
#endif
  va_end(args);
}

void LAppPal::PrintMessage(const csmChar* message) { PrintLog("%s", message); }

std::wstring LAppPal::StringToWString(const std::string& str) {
  std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
  return converter.from_bytes(str);
}

std::string LAppPal::WStringToString(const std::wstring& str) {
  std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
  return converter.to_bytes(str);
}

double LAppPal::EaseInOut(int x) {
  if (x <= 0) {
    return 0;
  }
  if (x >= 100) {
    return 100;
  }
  if (x <= 69) {
    // 30*(1-(1-0.03x)^3)
    return 30.0 * (1.0 - pow(1 - 0.03 * double(x), 3));
  }
  return 100 * (1 - 0.5 * pow(-0.03 * double(x) + 2.94, 3));
}


std::vector<std::wstring> LAppPal::ListFolder(const std::wstring& folder_path) {
    std::vector<std::wstring> ret;
    std::wstring searchPath = folder_path + L"\\*.mp3";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            // 检查是不是目录，如果不是目录则输出文件名
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                ret.push_back(std::wstring(findData.cFileName));
            }
        } while (FindNextFileW(hFind, &findData));
        FindClose(hFind);
    } else {
        std::wcout << L"Failed to read directory." << std::endl;
    }
  return ret;
}
