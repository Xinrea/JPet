#pragma once

#include <cstdint>
#include <string>

namespace Voice {
// A key that turns the microphone on. Each is read by polling the key state,
// which needs no extra system permission; keys that type text or switch the
// input method are not offered. The first key is the default.
struct ShortcutKey {
  const char* id;
  const char* name;
  uint32_t code;  // macOS: NSEventModifierFlags mask; Windows: virtual-key code.
};
#ifdef __APPLE__
// Right-hand keys use the device-dependent modifier bits (NX_DEVICE*KEYMASK).
inline constexpr ShortcutKey ShortcutKeys[] = {
  {"option", "Option", 1u << 19},           // NSEventModifierFlagOption, either side
  {"right_option", "右 Option", 0x40},      // NX_DEVICERALTKEYMASK
  {"right_command", "右 Command", 0x10},    // NX_DEVICERCMDKEYMASK
  {"control", "Control", 1u << 18},         // NSEventModifierFlagControl, either side
};
#else
inline constexpr ShortcutKey ShortcutKeys[] = {
  {"ctrl", "Ctrl", 0x11},         // VK_CONTROL, either side
  {"right_ctrl", "右 Ctrl", 0xA3},  // VK_RCONTROL
  {"right_alt", "右 Alt", 0xA5},    // VK_RMENU
};
#endif

// A saved key this platform does not offer (or none) falls back to the default.
inline const ShortcutKey& FindShortcutKey(const std::string& id) {
  for (const auto& key : ShortcutKeys)
    if (id == key.id) return key;
  return ShortcutKeys[0];
}
inline bool ValidShortcutKey(const std::string& id) { return FindShortcutKey(id).id == id; }

// Device-independent key edges, including recovery while the modifier is held.
class ShortcutControl {
 public:
  enum class Action { None, Begin, End };
  Action Update(bool held, bool holdMode, bool microphoneOn) {
    const bool pressed = held && !held_;
    const bool released = !held && held_;
    held_ = held;
    if (!held) blocked_ = false;
    if (blocked_) return Action::None;
    if (holdMode) {
      if (pressed && !microphoneOn) return Action::Begin;
      if (released && microphoneOn) return Action::End;
    } else if (pressed) return microphoneOn ? Action::End : Action::Begin;
    return Action::None;
  }
  void BlockUntilRelease(bool held) { held_ = held; blocked_ = held; }
  void BlockUntilRelease() { blocked_ = held_; }

 private:
  bool held_ = false;
  bool blocked_ = false;
};
}  // namespace Voice
