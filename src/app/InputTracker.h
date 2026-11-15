// Turns raw held-button state into presses, releases and key repeat. Shared by all platforms, so
// repeat timing and button chords behave the same everywhere.
#pragma once
#include <cstdint>

#include "platform/Platform.h"

namespace twill {

class InputTracker {
 public:
  void update(uint32_t held, uint64_t now_us);

  bool held(uint32_t buttons) const { return (held_ & buttons) == buttons; }
  bool any_held() const { return held_ != 0; }
  bool pressed(uint32_t b) const { return (pressed_ & b) != 0; }   // went down this frame
  bool released(uint32_t b) const { return (released_ & b) != 0; }
  // Pressed this frame, or auto-repeating while held (for the d-pad).
  bool pressed_or_repeat(uint32_t b) const { return ((pressed_ | repeat_) & b) != 0; }
  uint32_t held_mask() const { return held_; }

 private:
  static constexpr uint32_t kRepeatable = kUp | kDown | kLeft | kRight;
  static constexpr uint64_t kRepeatDelayUs = 280000;
  static constexpr uint64_t kRepeatIntervalUs = 55000;

  uint32_t held_ = 0, pressed_ = 0, released_ = 0, repeat_ = 0;
  uint64_t next_repeat_[kNumButtons] = {};
};

}  // namespace twill
