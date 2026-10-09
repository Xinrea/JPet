#pragma once

namespace Voice {
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
