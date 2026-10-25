#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Voice {
#ifdef __APPLE__
#define JPET_KEY_NAME(mac, windows) mac
#else
#define JPET_KEY_NAME(mac, windows) windows
#endif

// Keys a recorded voice shortcut may use, identified by the panel's
// KeyboardEvent.code. Shortcuts are read by polling key state, so keys the
// system handles itself (Fn, Caps Lock, Print Screen, media keys) are absent.
enum class KeyKind : uint8_t {
  Modifier,  // Ctrl/Alt(Option)/Shift/Win(Command)
  Function,  // F1-F24, Pause, Scroll Lock: usable alone
  Typing,    // Types text or moves the caret: needs a modifier other than Shift
};
inline constexpr uint32_t NoKey = UINT32_MAX;  // Not on this platform's keyboards.
struct Key {
  const char* code;
  const char* name;
  KeyKind kind;
  uint32_t windows;  // Virtual-key code.
  uint32_t mac;      // Modifiers: NSEventModifierFlags mask (NX_DEVICE*KEYMASK for one side); others: kVK_* code.
};
// Modifiers come first, in the order shortcuts are written.
inline constexpr Key Keys[] = {
  {"Control", JPET_KEY_NAME("Control", "Ctrl"), KeyKind::Modifier, 0x11, 1u << 18},
  {"ControlLeft", JPET_KEY_NAME("左 Control", "左 Ctrl"), KeyKind::Modifier, 0xA2, 0x0001},
  {"ControlRight", JPET_KEY_NAME("右 Control", "右 Ctrl"), KeyKind::Modifier, 0xA3, 0x2000},
  {"Alt", JPET_KEY_NAME("Option", "Alt"), KeyKind::Modifier, 0x12, 1u << 19},
  {"AltLeft", JPET_KEY_NAME("左 Option", "左 Alt"), KeyKind::Modifier, 0xA4, 0x0020},
  {"AltRight", JPET_KEY_NAME("右 Option", "右 Alt"), KeyKind::Modifier, 0xA5, 0x0040},
  {"Shift", "Shift", KeyKind::Modifier, 0x10, 1u << 17},
  {"ShiftLeft", "左 Shift", KeyKind::Modifier, 0xA0, 0x0002},
  {"ShiftRight", "右 Shift", KeyKind::Modifier, 0xA1, 0x0004},
  {"MetaLeft", JPET_KEY_NAME("左 Command", "左 Win"), KeyKind::Modifier, 0x5B, 0x0008},
  {"MetaRight", JPET_KEY_NAME("右 Command", "右 Win"), KeyKind::Modifier, 0x5C, 0x0010},
  {"F1", "F1", KeyKind::Function, 0x70, 0x7A}, {"F2", "F2", KeyKind::Function, 0x71, 0x78},
  {"F3", "F3", KeyKind::Function, 0x72, 0x63}, {"F4", "F4", KeyKind::Function, 0x73, 0x76},
  {"F5", "F5", KeyKind::Function, 0x74, 0x60}, {"F6", "F6", KeyKind::Function, 0x75, 0x61},
  {"F7", "F7", KeyKind::Function, 0x76, 0x62}, {"F8", "F8", KeyKind::Function, 0x77, 0x64},
  {"F9", "F9", KeyKind::Function, 0x78, 0x65}, {"F10", "F10", KeyKind::Function, 0x79, 0x6D},
  {"F11", "F11", KeyKind::Function, 0x7A, 0x67}, {"F12", "F12", KeyKind::Function, 0x7B, 0x6F},
  {"F13", "F13", KeyKind::Function, 0x7C, 0x69}, {"F14", "F14", KeyKind::Function, 0x7D, 0x6B},
  {"F15", "F15", KeyKind::Function, 0x7E, 0x71}, {"F16", "F16", KeyKind::Function, 0x7F, 0x6A},
  {"F17", "F17", KeyKind::Function, 0x80, 0x40}, {"F18", "F18", KeyKind::Function, 0x81, 0x4F},
  {"F19", "F19", KeyKind::Function, 0x82, 0x50}, {"F20", "F20", KeyKind::Function, 0x83, 0x5A},
  {"F21", "F21", KeyKind::Function, 0x84, NoKey}, {"F22", "F22", KeyKind::Function, 0x85, NoKey},
  {"F23", "F23", KeyKind::Function, 0x86, NoKey}, {"F24", "F24", KeyKind::Function, 0x87, NoKey},
  {"Pause", "Pause", KeyKind::Function, 0x13, NoKey},
  {"ScrollLock", "Scroll Lock", KeyKind::Function, 0x91, NoKey},
  {"KeyA", "A", KeyKind::Typing, 'A', 0x00}, {"KeyB", "B", KeyKind::Typing, 'B', 0x0B},
  {"KeyC", "C", KeyKind::Typing, 'C', 0x08}, {"KeyD", "D", KeyKind::Typing, 'D', 0x02},
  {"KeyE", "E", KeyKind::Typing, 'E', 0x0E}, {"KeyF", "F", KeyKind::Typing, 'F', 0x03},
  {"KeyG", "G", KeyKind::Typing, 'G', 0x05}, {"KeyH", "H", KeyKind::Typing, 'H', 0x04},
  {"KeyI", "I", KeyKind::Typing, 'I', 0x22}, {"KeyJ", "J", KeyKind::Typing, 'J', 0x26},
  {"KeyK", "K", KeyKind::Typing, 'K', 0x28}, {"KeyL", "L", KeyKind::Typing, 'L', 0x25},
  {"KeyM", "M", KeyKind::Typing, 'M', 0x2E}, {"KeyN", "N", KeyKind::Typing, 'N', 0x2D},
  {"KeyO", "O", KeyKind::Typing, 'O', 0x1F}, {"KeyP", "P", KeyKind::Typing, 'P', 0x23},
  {"KeyQ", "Q", KeyKind::Typing, 'Q', 0x0C}, {"KeyR", "R", KeyKind::Typing, 'R', 0x0F},
  {"KeyS", "S", KeyKind::Typing, 'S', 0x01}, {"KeyT", "T", KeyKind::Typing, 'T', 0x11},
  {"KeyU", "U", KeyKind::Typing, 'U', 0x20}, {"KeyV", "V", KeyKind::Typing, 'V', 0x09},
  {"KeyW", "W", KeyKind::Typing, 'W', 0x0D}, {"KeyX", "X", KeyKind::Typing, 'X', 0x07},
  {"KeyY", "Y", KeyKind::Typing, 'Y', 0x10}, {"KeyZ", "Z", KeyKind::Typing, 'Z', 0x06},
  {"Digit0", "0", KeyKind::Typing, '0', 0x1D}, {"Digit1", "1", KeyKind::Typing, '1', 0x12},
  {"Digit2", "2", KeyKind::Typing, '2', 0x13}, {"Digit3", "3", KeyKind::Typing, '3', 0x14},
  {"Digit4", "4", KeyKind::Typing, '4', 0x15}, {"Digit5", "5", KeyKind::Typing, '5', 0x17},
  {"Digit6", "6", KeyKind::Typing, '6', 0x16}, {"Digit7", "7", KeyKind::Typing, '7', 0x1A},
  {"Digit8", "8", KeyKind::Typing, '8', 0x1C}, {"Digit9", "9", KeyKind::Typing, '9', 0x19},
  {"Backquote", "`", KeyKind::Typing, 0xC0, 0x32}, {"Minus", "-", KeyKind::Typing, 0xBD, 0x1B},
  {"Equal", "=", KeyKind::Typing, 0xBB, 0x18}, {"BracketLeft", "[", KeyKind::Typing, 0xDB, 0x21},
  {"BracketRight", "]", KeyKind::Typing, 0xDD, 0x1E}, {"Backslash", "\\", KeyKind::Typing, 0xDC, 0x2A},
  {"Semicolon", ";", KeyKind::Typing, 0xBA, 0x29}, {"Quote", "'", KeyKind::Typing, 0xDE, 0x27},
  {"Comma", ",", KeyKind::Typing, 0xBC, 0x2B}, {"Period", ".", KeyKind::Typing, 0xBE, 0x2F},
  {"Slash", "/", KeyKind::Typing, 0xBF, 0x2C},
  {"Space", "空格", KeyKind::Typing, 0x20, 0x31}, {"Tab", "Tab", KeyKind::Typing, 0x09, 0x30},
  {"Enter", JPET_KEY_NAME("Return", "Enter"), KeyKind::Typing, 0x0D, 0x24},
  {"Backspace", JPET_KEY_NAME("Delete", "Backspace"), KeyKind::Typing, 0x08, 0x33},
  {"Escape", "Esc", KeyKind::Typing, 0x1B, 0x35},
  {"Insert", "Insert", KeyKind::Typing, 0x2D, NoKey},
  {"Delete", JPET_KEY_NAME("⌦", "Delete"), KeyKind::Typing, 0x2E, 0x75},
  {"Home", "Home", KeyKind::Typing, 0x24, 0x73}, {"End", "End", KeyKind::Typing, 0x23, 0x77},
  {"PageUp", "Page Up", KeyKind::Typing, 0x21, 0x74}, {"PageDown", "Page Down", KeyKind::Typing, 0x22, 0x79},
  {"ArrowLeft", "←", KeyKind::Typing, 0x25, 0x7B}, {"ArrowUp", "↑", KeyKind::Typing, 0x26, 0x7E},
  {"ArrowRight", "→", KeyKind::Typing, 0x27, 0x7C}, {"ArrowDown", "↓", KeyKind::Typing, 0x28, 0x7D},
  {"Numpad0", "小键盘 0", KeyKind::Typing, 0x60, 0x52}, {"Numpad1", "小键盘 1", KeyKind::Typing, 0x61, 0x53},
  {"Numpad2", "小键盘 2", KeyKind::Typing, 0x62, 0x54}, {"Numpad3", "小键盘 3", KeyKind::Typing, 0x63, 0x55},
  {"Numpad4", "小键盘 4", KeyKind::Typing, 0x64, 0x56}, {"Numpad5", "小键盘 5", KeyKind::Typing, 0x65, 0x57},
  {"Numpad6", "小键盘 6", KeyKind::Typing, 0x66, 0x58}, {"Numpad7", "小键盘 7", KeyKind::Typing, 0x67, 0x59},
  {"Numpad8", "小键盘 8", KeyKind::Typing, 0x68, 0x5B}, {"Numpad9", "小键盘 9", KeyKind::Typing, 0x69, 0x5C},
  {"NumpadMultiply", "小键盘 *", KeyKind::Typing, 0x6A, 0x43}, {"NumpadAdd", "小键盘 +", KeyKind::Typing, 0x6B, 0x45},
  {"NumpadSubtract", "小键盘 -", KeyKind::Typing, 0x6D, 0x4E}, {"NumpadDecimal", "小键盘 .", KeyKind::Typing, 0x6E, 0x41},
  {"NumpadDivide", "小键盘 /", KeyKind::Typing, 0x6F, 0x4B},
};
// Letters, digits, symbols and Shift combinations would fire while typing.
inline constexpr const char* ShortcutTypingError = JPET_KEY_NAME(
    "单独的字母、数字、符号或只搭配 Shift 会在打字时误触，请搭配 Control、Option 或 Command，或使用 F1–F20 等功能键",
    "单独的字母、数字、符号或只搭配 Shift 会在打字时误触，请搭配 Ctrl、Alt 或 Win，或使用 F1–F24 等功能键");
#undef JPET_KEY_NAME

constexpr const Key* FindKey(std::string_view code) {
  for (const auto& key : Keys)
    if (code == key.code) return &key;
  return nullptr;
}
// For checking the table against the platform SDK's constants at compile time.
constexpr uint32_t KeyCode(std::string_view code, bool mac) {
  const auto* key = FindKey(code);
  return key ? (mac ? key->mac : key->windows) : NoKey;
}

inline uint32_t NativeKey(const Key& key) {
#ifdef __APPLE__
  return key.mac;
#else
  return key.windows;
#endif
}

// A combination of up to four keys, held together.
struct Shortcut {
  std::vector<const Key*> keys;  // In Keys order.
  std::string Code() const {     // "ControlLeft+Space", as saved and sent to the panel.
    std::string out;
    for (const auto* key : keys) out += (out.empty() ? "" : "+") + std::string(key->code);
    return out;
  }
  std::string Name() const {
    std::string out;
    for (const auto* key : keys) out += (out.empty() ? "" : " + ") + std::string(key->name);
    return out;
  }
  bool ModifiersOnly() const {
    return std::all_of(keys.begin(), keys.end(), [](const Key* key) { return key->kind == KeyKind::Modifier; });
  }
};
constexpr size_t MaxShortcutKeys = 4;

// Parses a shortcut recorded in the panel; an empty one is the default.
// Rejects keys this platform cannot read, and combinations that type text.
inline std::optional<Shortcut> ParseShortcut(const std::string& text, std::string& error) {
#ifdef __APPLE__
  const std::string value = text.empty() ? "Alt" : text;
#else
  const std::string value = text.empty() ? "Control" : text;
#endif
  std::vector<bool> used(std::size(Keys));
  for (size_t start = 0; start <= value.size();) {
    const auto end = std::min(value.find('+', start), value.size());
    const auto* found = FindKey(std::string_view(value).substr(start, end - start));
    if (!found || NativeKey(*found) == NoKey) {
      error = "这个键不能用作快捷键，请换一个";
      return std::nullopt;
    }
    used[found - std::begin(Keys)] = true;
    start = end + 1;
  }
  Shortcut shortcut;
  for (size_t i = 0; i < std::size(Keys); ++i) if (used[i]) shortcut.keys.push_back(&Keys[i]);
  if (shortcut.keys.size() > MaxShortcutKeys) {
    error = "快捷键最多包含 4 个键";
    return std::nullopt;
  }
  const bool standalone = std::any_of(shortcut.keys.begin(), shortcut.keys.end(), [](const Key* key) {
    return key->kind == KeyKind::Function || (key->kind == KeyKind::Modifier && std::string(key->code).rfind("Shift", 0) != 0);
  });
  if (!standalone) {
    error = ShortcutTypingError;
    return std::nullopt;
  }
  return shortcut;
}

// The key used before shortcuts could be recorded: either Option (macOS) or Ctrl.
inline Shortcut DefaultShortcut() {
  std::string error;
  return *ParseShortcut("", error);
}

// A saved shortcut this platform cannot use (e.g. copied from Windows) falls back to the default.
inline Shortcut SavedShortcut(const std::string& text) {
  std::string error;
  if (auto shortcut = ParseShortcut(text, error)) return *shortcut;
  return DefaultShortcut();
}

// Device-independent key edges, including recovery while the keys are held.
// Hold mode waits HoldDelay before it interrupts a reply or shows recording:
// Prepare may open the microphone at once so no words are lost, Begin
// confirms the turn, and a shorter press (an accidental Ctrl+C) is Cancelled.
class ShortcutControl {
 public:
  using Clock = std::chrono::steady_clock;
  static constexpr std::chrono::milliseconds HoldDelay{200};
  enum class Action { None, Prepare, Begin, Cancel, End };
  Action Update(bool held, bool holdMode, bool microphoneOn, Clock::time_point now) {
    const bool pressed = held && !held_;
    const bool released = !held && held_;
    held_ = held;
    if (!held) blocked_ = false;
    if (blocked_) return Action::None;
    if (preparing_) {
      if (released || !holdMode) { preparing_ = false; return Action::Cancel; }
      if (now - pressedAt_ < HoldDelay) return Action::None;
      preparing_ = false;
      return Action::Begin;
    }
    if (holdMode) {
      if (pressed && !microphoneOn) { preparing_ = true; pressedAt_ = now; return Action::Prepare; }
      if (released && microphoneOn) return Action::End;
    } else if (pressed) return microphoneOn ? Action::End : Action::Begin;
    return Action::None;
  }
  // The owner stops any prepared capture itself.
  void BlockUntilRelease(bool held) { held_ = held; blocked_ = held; preparing_ = false; }
  void BlockUntilRelease() { blocked_ = held_; preparing_ = false; }
  bool Preparing() const { return preparing_; }

 private:
  bool held_ = false, blocked_ = false, preparing_ = false;
  Clock::time_point pressedAt_;
};
}  // namespace Voice
