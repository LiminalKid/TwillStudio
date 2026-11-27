// Turns a platform's pointer events (a mouse, fingers) into gestures the app acts on: tap, double
// tap, drag, long press, wheel, and two fingers' pan and pinch. The same gestures come from a mouse
// and from a touch screen where they can, so whatever a mouse does has a way to be done by touch:
// a right-click or Shift-click is a shortcut, never the only way.
//
//   Press       a button or finger went down: at once, for what a drag can't mean anything else for
//               (taps 2: a second soon after the first, in the same place)
//   Tap         let go of again without moving far (taps as its Press had)
//   LongPress   a finger held still a moment (not the mouse); a drag may follow, with long_press set
//   DragStart   moved past the slop with a button or finger down: start_x/y is where it went down
//   Drag        each move after, dx/dy since the last; DragEnd when let go (Cancel if a second
//               finger came down first, or the drag was taken away)
//   Wheel       a mouse wheel or a trackpad's scrolling, in notches
//   Pan, Pinch  two fingers: their middle moved by dx/dy; their spread changed by `scale`
#pragma once
#include <cstdint>
#include <vector>

#include "platform/Platform.h"

namespace twill {

struct Gesture {
  enum Kind : uint8_t { Press, Tap, LongPress, DragStart, Drag, DragEnd, Cancel, Wheel, Pan, Pinch };
  Kind kind = Tap;
  PointerEvent::Source source = PointerEvent::Mouse;
  int button = 1;  // 2: the mouse's right button
  bool shift = false;
  int x = 0, y = 0;              // where it is now
  int start_x = 0, start_y = 0;  // where it went down
  int dx = 0, dy = 0;            // Drag and Pan: moved since the last
  float wheel_x = 0, wheel_y = 0;
  float scale = 1;               // Pinch
  int taps = 1;                  // Press and Tap: 2 for a double tap
  bool long_press = false;       // a drag that began with a long press
  bool touch() const { return source != PointerEvent::Mouse; }
};

class PointerTracker {
 public:
  // `slop`: how far, in pixels, a mouse may move and still tap; a finger gets about three times
  // as much.
  void update(const std::vector<PointerEvent>& events, uint64_t now_us, float slop);
  const std::vector<Gesture>& gestures() const { return out_; }
  // The drag going on is over for whoever had it: the rest of it (to the release) is dropped.
  void cancel();

  static constexpr uint64_t kLongPressUs = 450000;
  static constexpr uint64_t kDoubleTapUs = 350000;

 private:
  enum class State { Idle, Pending, LongPressed, Dragging, TwoFingers, Ignoring };
  void emit(Gesture::Kind kind, int x, int y);
  float slop_for(PointerEvent::Source s) const { return s == PointerEvent::Mouse ? slop_ : slop_ * 3; }
  void two_finger_metrics(int& cx, int& cy, float& spread) const;

  std::vector<Gesture> out_;
  State state_ = State::Idle;
  float slop_ = 4;
  int id_ = 0, button_ = 1;
  PointerEvent::Source source_ = PointerEvent::Mouse;
  bool shift_ = false, long_press_ = false;
  int down_x_ = 0, down_y_ = 0, last_x_ = 0, last_y_ = 0;
  uint64_t down_us_ = 0;
  // The fingers down now, and two fingers' last middle and spread.
  struct Finger {
    int id, x, y;
  };
  std::vector<Finger> fingers_;
  int pan_x_ = 0, pan_y_ = 0;
  float spread_ = 0;
  // The last tap, for a double tap.
  uint64_t tap_us_ = 0;
  int tap_x_ = 0, tap_y_ = 0, tap_button_ = 0, tap_count_ = 0;
  int count_ = 1;  // the press going on: 1, or 2 for a double
};

}  // namespace twill
