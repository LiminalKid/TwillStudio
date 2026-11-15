#include "app/InputTracker.h"

namespace twill {

const char* button_name(Button b) {
  switch (b) {
    case kUp: return "Up";
    case kDown: return "Down";
    case kLeft: return "Left";
    case kRight: return "Right";
    case kA: return "A";
    case kB: return "B";
    case kX: return "X";
    case kY: return "Y";
    case kL: return "L";
    case kR: return "R";
    case kL2: return "L2";
    case kR2: return "R2";
    case kStart: return "Start";
    case kSelect: return "Select";
    case kMenu: return "Menu";
  }
  return "?";
}

void InputTracker::update(uint32_t held, uint64_t now) {
  pressed_ = held & ~held_;
  released_ = held_ & ~held;
  held_ = held;
  repeat_ = 0;
  for (int i = 0; i < kNumButtons; ++i) {
    uint32_t bit = 1u << i;
    if (!(bit & kRepeatable)) continue;
    if (pressed_ & bit) {
      next_repeat_[i] = now + kRepeatDelayUs;
    } else if ((held_ & bit) && now >= next_repeat_[i]) {
      repeat_ |= bit;
      next_repeat_[i] = now + kRepeatIntervalUs;
    }
  }
}

}  // namespace twill
