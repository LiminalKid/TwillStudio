#include "app/Pointer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace twill {

void PointerTracker::emit(Gesture::Kind kind, int x, int y) {
  Gesture g;
  g.kind = kind;
  g.source = source_;
  g.button = button_;
  g.shift = shift_;
  g.x = x;
  g.y = y;
  g.start_x = down_x_;
  g.start_y = down_y_;
  g.dx = x - last_x_;
  g.dy = y - last_y_;
  g.long_press = long_press_;
  out_.push_back(g);
}

void PointerTracker::cancel() {
  if (state_ == State::Dragging || state_ == State::Pending || state_ == State::LongPressed)
    state_ = State::Ignoring;
}

void PointerTracker::two_finger_metrics(int& cx, int& cy, float& spread) const {
  const Finger& a = fingers_[0];
  const Finger& b = fingers_[1];
  cx = (a.x + b.x) / 2;
  cy = (a.y + b.y) / 2;
  spread = std::hypot(float(a.x - b.x), float(a.y - b.y));
}

void PointerTracker::update(const std::vector<PointerEvent>& events, uint64_t now_us, float slop) {
  out_.clear();
  slop_ = slop;
  for (const PointerEvent& e : events) {
    if (e.type == PointerEvent::Wheel) {
      Gesture g;
      g.kind = Gesture::Wheel;
      g.source = e.source;
      g.shift = e.shift;
      g.x = g.start_x = e.x;
      g.y = g.start_y = e.y;
      g.wheel_x = e.wheel_x;
      g.wheel_y = e.wheel_y;
      out_.push_back(g);
      continue;
    }
    const bool finger = e.source != PointerEvent::Mouse;
    if (finger) {
      auto it = std::find_if(fingers_.begin(), fingers_.end(), [&](const Finger& f) { return f.id == e.id; });
      if (e.type == PointerEvent::Down) {
        if (it == fingers_.end()) fingers_.push_back(Finger{e.id, e.x, e.y});
      } else if (it != fingers_.end()) {
        if (e.type == PointerEvent::Move) {
          it->x = e.x;
          it->y = e.y;
        } else {
          fingers_.erase(it);
        }
      }
      // A second finger: two-finger pan and pinch, and whatever the first had begun is called off.
      if (e.type == PointerEvent::Down && fingers_.size() == 2 && state_ != State::TwoFingers) {
        if (state_ == State::Dragging) emit(Gesture::Cancel, last_x_, last_y_);
        state_ = State::TwoFingers;
        two_finger_metrics(pan_x_, pan_y_, spread_);
        continue;
      }
      if (state_ == State::TwoFingers) {
        if (fingers_.size() >= 2 && e.type == PointerEvent::Move) {
          int cx, cy;
          float spread;
          two_finger_metrics(cx, cy, spread);
          Gesture g;
          g.kind = Gesture::Pan;
          g.source = e.source;
          g.x = cx;
          g.y = cy;
          g.dx = cx - pan_x_;
          g.dy = cy - pan_y_;
          if (g.dx || g.dy) out_.push_back(g);
          if (spread_ > 0 && spread > 0 && std::fabs(spread - spread_) >= 1) {
            g.kind = Gesture::Pinch;
            g.dx = g.dy = 0;
            g.scale = spread / spread_;
            out_.push_back(g);
            spread_ = spread;
          }
          pan_x_ = cx;
          pan_y_ = cy;
        }
        if (fingers_.empty()) state_ = State::Idle;
        continue;
      }
    }

    switch (e.type) {
      case PointerEvent::Down:
        if (state_ != State::Idle) break;  // another button or finger while one is down: ignored
        state_ = State::Pending;
        id_ = e.id;
        button_ = e.button;
        source_ = e.source;
        shift_ = e.shift;
        long_press_ = false;
        down_x_ = last_x_ = e.x;
        down_y_ = last_y_ = e.y;
        down_us_ = now_us;
        {
          // A second press soon after a tap, in the same place: a double.
          const bool again = tap_count_ == 1 && now_us - tap_us_ <= kDoubleTapUs && tap_button_ == button_ &&
                             std::hypot(float(e.x - tap_x_), float(e.y - tap_y_)) <= slop_for(source_) * 2;
          count_ = again ? 2 : 1;
        }
        emit(Gesture::Press, e.x, e.y);
        out_.back().taps = count_;
        break;
      case PointerEvent::Move:
        if (e.id != id_ || e.source != source_) break;
        if (state_ == State::Pending || state_ == State::LongPressed) {
          if (std::hypot(float(e.x - down_x_), float(e.y - down_y_)) > slop_for(source_)) {
            long_press_ = state_ == State::LongPressed;
            state_ = State::Dragging;
            last_x_ = down_x_;
            last_y_ = down_y_;
            emit(Gesture::DragStart, down_x_, down_y_);
            emit(Gesture::Drag, e.x, e.y);
            last_x_ = e.x;
            last_y_ = e.y;
          }
        } else if (state_ == State::Dragging) {
          emit(Gesture::Drag, e.x, e.y);
          last_x_ = e.x;
          last_y_ = e.y;
        }
        break;
      case PointerEvent::Up:
      case PointerEvent::Cancel:
        if (e.id != id_ || e.source != source_ || (!finger && e.button != button_)) {
          if (finger && fingers_.empty() && state_ == State::Ignoring) state_ = State::Idle;
          break;
        }
        if (state_ == State::Dragging) {
          emit(e.type == PointerEvent::Up ? Gesture::DragEnd : Gesture::Cancel, e.x, e.y);
        } else if (state_ == State::Pending && e.type == PointerEvent::Up) {
          tap_count_ = count_;
          tap_us_ = now_us;
          tap_x_ = e.x;
          tap_y_ = e.y;
          tap_button_ = button_;
          emit(Gesture::Tap, down_x_, down_y_);
          out_.back().taps = tap_count_;
        }
        state_ = State::Idle;
        break;
      case PointerEvent::Wheel:
        break;
    }
  }
  // A finger held still: a long press (a mouse has right-click and Shift for what it does).
  if (state_ == State::Pending && source_ != PointerEvent::Mouse && now_us - down_us_ >= kLongPressUs) {
    state_ = State::LongPressed;
    emit(Gesture::LongPress, down_x_, down_y_);
  }
}

}  // namespace twill
