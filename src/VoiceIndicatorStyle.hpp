#pragma once

#include <algorithm>
#include <chrono>
#include <string>

// Logical pixels on Windows, points on macOS. Keep both native HUDs in sync.
namespace Voice::Indicator {
struct Content {
  std::string text, shortcutHint;
  bool error = false, microphoneOn = false;
};

inline std::string ShortcutHint(const char* shortcut, bool holdMode) {
  return std::string(holdMode ? "按住 " : "按 ") + shortcut +
      (holdMode ? " 开始收音" : " 开启/关闭收音");
}

// Warnings/errors must not be hidden behind an earlier or partial reply.
inline std::string BodyText(const std::string& reply, const std::string& status, bool notice) {
  return notice || reply.empty() ? status : reply;
}

// The application tick drives animation; hidden/idle HUDs need no timer.
inline int PulseFrame() {
  return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count() / 40 % 30);
}

constexpr int Width = 400;
constexpr int MinHeight = 88;  // Keep room for the microphone and its pulse.
constexpr int MaxHeight = 136;
constexpr int BodyLeft = 60, BodyTop = 34, BodyRight = 16, BodyBottom = 12;
constexpr int BodyWidth = Width - BodyLeft - BodyRight;
constexpr int Radius = 14;

// Measure wrapped text with the native renderer, then add the shared padding.
inline int HeightForText(int textHeight) {
  return std::clamp(textHeight + BodyTop + BodyBottom, MinHeight, MaxHeight);
}
struct Color { int red, green, blue; };
constexpr Color Background{255, 255, 252};
constexpr Color Ink{102, 80, 68};
constexpr Color Border{209, 226, 185};
constexpr Color Accent{121, 201, 27};
constexpr Color AccentTop{157, 221, 58};
constexpr Color Soft{241, 248, 229};
constexpr Color Error{189, 93, 118};
constexpr Color ErrorAccent{222, 116, 145};
constexpr Color ErrorTop{239, 159, 179};
constexpr Color ErrorSoft{255, 241, 245};
}  // namespace Voice::Indicator
