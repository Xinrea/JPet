#pragma once
#include "Platform.hpp"

#include <string>

class LUtils {
 public:
  static void OpenURL(const std::string& url) {
    Platform::Open(url);
  }
};