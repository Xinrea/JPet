#pragma once
#include <string>

namespace Voice {
struct DesktopImage {
  std::string jpeg;
  int width = 0, height = 0, displayCount = 0;
};
// Runs on the tool worker. Images remain in memory and are never logged/saved.
bool CaptureDesktop(int display, DesktopImage& image, std::string& error);
} // namespace Voice
