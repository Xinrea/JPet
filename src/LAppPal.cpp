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
#include <cstdio>
#include <iostream>
#include <mutex>

#include "LAppDefine.hpp"

using std::endl;
using namespace Csm;
using namespace std;
using namespace LAppDefine;

double LAppPal::s_currentFrame = 0.0;
double LAppPal::s_lastFrame = 0.0;
double LAppPal::s_deltaTime = 0.0;
std::fstream LAppPal::s_logFile;

std::once_flag logFileFlag;

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
  std::call_once(logFileFlag, []() {
    s_logFile.open(documentPath + "\\JPetLog.txt",
                   std::ios::out | std::ios::app);
  });
  va_list args;
  csmChar buf[512];
  va_start(args, format);
  vsnprintf_s(buf, sizeof(buf), format, args);  // 標準出力でレンダリング
#ifdef CSM_DEBUG_MEMORY_LEAKING
  // メモリリークチェック時は大量の標準出力がはしり重いのでprintfを利用する
  std::printf(buf);
#else
  s_logFile << buf << std::endl;
  std::cerr << buf << std::endl;
#endif
  va_end(args);
}

void LAppPal::PrintMessage(const csmChar* message) { PrintLog("%s", message); }
