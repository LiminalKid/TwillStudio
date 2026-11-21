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
  // The buttons held now did their job (closing a panel, say): they aren't held for the views, and
  // letting them go isn't a release, until they're up again. (B closes a panel on its press; its
  // release would otherwise reach the arrangement and delete the clip under the cursor.)
  void consume_held() {
    if (raw_ & kModifiers) return;  // a chord's buttons are its own already
    chorded_ |= raw_;
    held_ = pressed_ = released_ = repeat_ = 0;
  }

 private:
  static constexpr uint32_t kRepeatable = kUp | kDown | kLeft | kRight;
  static constexpr uint64_t kRepeatDelayUs = 280000;
  static constexpr uint64_t kRepeatIntervalUs = 55000;

  // A button pressed while Select or Menu is held belongs to that chord until it's let go: once
  // the modifier is up, it isn't held for the views, and letting it go isn't a release. (Undo is
  // Select + B; B released after Select would otherwise reach the arrangement as a plain B and
  // delete the clip the undo had just brought back.)
  static constexpr uint32_t kModifiers = kSelect | kMenu;
  uint32_t held_ = 0, pressed_ = 0, released_ = 0, repeat_ = 0;
  uint32_t raw_ = 0, chorded_ = 0;
  uint64_t next_repeat_[kNumButtons] = {};
};

}  // namespace twill
