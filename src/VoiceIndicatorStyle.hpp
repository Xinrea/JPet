#pragma once

// Logical pixels on Windows, points on macOS. Keep both native HUDs in sync.
namespace Voice::Indicator {
constexpr int Width = 340;
constexpr int Height = 88;
constexpr int Radius = 14;
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
