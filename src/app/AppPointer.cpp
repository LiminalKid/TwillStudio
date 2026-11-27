// The pointer in the app's own panels and top bar (App.h, "The pointer"). Each view handles the
// pointer over its own content.
#include <cstdlib>

#include "app/App.h"
#include "app/ArrangementView.h"
#include "app/InstrumentView.h"
#include "app/MixerView.h"
#include "app/PianoRollView.h"

namespace twill {

void App::handle_pointer() {
  static const bool trace = [] {
    const char* v = std::getenv("TWILL_POINTER_LOG");
    return v && *v && *v != '0';
  }();
  for (const Gesture& g : pointer_.gestures()) {
    if (trace)
      p_.log("pointer: gesture %d at %d,%d (from %d,%d) taps %d; panel %s at %d,%d %dx%d, rows from y %d, %d high; "
             "notice %d, naming %d",
             int(g.kind), g.x, g.y, g.start_x, g.start_y, g.taps, inspector_open() ? "open" : "shut", inspector_rect_.x,
             inspector_rect_.y, inspector_rect_.w, inspector_rect_.h, inspector_list_.y, inspector_row_h_,
             int(notice_open()), int(name_edit_));
    if (name_edit_) continue;
    if (notice_open()) {
      if (g.kind == Gesture::Press) {
        notice_title_.clear();
        notice_text_.clear();
        invalidate_all();
      }
      continue;
    }
    if (inspector_open()) {
      pointer_inspector(g);
      // A drag a view began before the panel opened: it's let go of there too.
      if (g.kind == Gesture::DragEnd || g.kind == Gesture::Cancel) pointer_view(g);
      continue;
    }
    if (pointer_top_bar(g)) continue;
    pointer_view(g);
  }
}

void App::pointer_view(const Gesture& g) {
  switch (view_) {
    case View::Arrangement: arrangement_->pointer(g); break;
    case View::PianoRoll: roll_->pointer(g); break;
    case View::Mixer: mixer_->pointer(g); break;
    case View::Instrument: instrument_->pointer(g); break;
  }
}

bool App::pointer_top_bar(const Gesture& g) {
  if (!top_bar_.contains(g.start_x, g.start_y)) return false;
  if (g.kind != Gesture::Press || g.button != 1) return true;
  for (View v : {View::Arrangement, View::PianoRoll, View::Instrument, View::Mixer}) {
    if (tab_rects_[int(v)].contains(g.x, g.y)) {
      if (v != view_) show(v);
      return true;
    }
  }
  if (transport_rect_.contains(g.x, g.y)) start_or_stop();
  return true;
}

int App::inspector_row_at(int x, int y) const {
  if (!inspector_open() || !inspector_list_.contains(x, y)) return -1;
  const int row = inspector_scroll_ + (y - inspector_list_.y) / inspector_row_h_;
  return row < int(inspector_fields_.size()) ? row : -1;
}

Rect App::inspector_row_rect(int row) const {
  if (!inspector_open() || row < inspector_scroll_ || row >= int(inspector_fields_.size())) return Rect{};
  const int y = inspector_list_.y + (row - inspector_scroll_) * inspector_row_h_;
  return Rect{inspector_list_.x, y, inspector_list_.w, inspector_row_h_}.intersect(inspector_list_);
}

void App::pointer_inspector(const Gesture& g) {
  switch (g.kind) {
    case Gesture::Press: {
      // At once, on the press: a right-click, or a press off the panel, puts it away (B); a row
      // not selected is selected; a selected row whose job is a press (A) does it, from a mouse.
      press_was_selected_ = press_acted_ = false;
      if (g.button == 2 || !inspector_rect_.contains(g.x, g.y)) {
        tap_acted_ = false;
        close_inspector();
        return;
      }
      const int row = inspector_row_at(g.x, g.y);
      if (row < 0 || inspector_fields_[size_t(row)].heading) return;
      // The second of a double click whose first did the row's job (opened another panel, say): not
      // for what's under it now.
      if (g.taps == 2 && tap_acted_) {
        tap_acted_ = false;
        press_acted_ = true;
        return;
      }
      tap_acted_ = false;
      if (row != inspector_sel_) {
        inspector_sel_ = row;
        invalidate_all();
        return;
      }
      press_was_selected_ = true;
      InspectorField& f = inspector_fields_[size_t(row)];
      // A finger waits for its tap: it may be scrolling the rows.
      if (f.press && !f.adjust && !g.touch()) {
        auto press = f.press;  // may close or replace the panel
        press();
        tap_acted_ = press_acted_ = true;
        invalidate_all();
      }
      return;
    }
    case Gesture::Tap: {
      // Let go of on a row that was selected when pressed: its job (a finger's), or a step of its
      // value (Shift-click: the other way).
      if (!press_was_selected_ || press_acted_) return;
      press_was_selected_ = false;
      const int row = inspector_row_at(g.x, g.y);
      if (row != inspector_sel_ || row < 0) return;
      InspectorField& f = inspector_fields_[size_t(row)];
      if (f.press) {
        auto press = f.press;
        press();
        tap_acted_ = true;
      } else if (f.adjust) {
        auto adjust = f.adjust;
        adjust(g.shift ? -1 : 1, false);
      }
      invalidate_all();
      return;
    }
    case Gesture::DragStart:
      // What a drag does is settled by its first move: across a row that changes, its value; up
      // and down, through the rows.
      panel_drag_ = inspector_rect_.contains(g.start_x, g.start_y) ? PanelDrag::None : PanelDrag::Off;
      panel_drag_row_ = inspector_row_at(g.start_x, g.start_y);
      panel_drag_acc_ = 0;
      return;
    case Gesture::Drag:
    case Gesture::Pan: {
      if (g.kind == Gesture::Drag && panel_drag_ == PanelDrag::None) {
        const int row = panel_drag_row_;
        const bool across = std::abs(g.x - g.start_x) > std::abs(g.y - g.start_y);
        if (across && row >= 0 && !inspector_fields_[size_t(row)].heading && inspector_fields_[size_t(row)].adjust) {
          panel_drag_ = PanelDrag::Adjust;
          inspector_sel_ = row;
          invalidate_all();
        } else {
          panel_drag_ = PanelDrag::Scroll;
        }
      }
      if (g.kind == Gesture::Pan) {
        if (!inspector_rect_.contains(g.x, g.y)) return;
      } else if (panel_drag_ == PanelDrag::Off) {
        return;
      }
      if (g.kind == Gesture::Drag && panel_drag_ == PanelDrag::Adjust) {
        const float step = u(12);
        panel_drag_acc_ += float(g.dx);
        while (std::abs(panel_drag_acc_) >= step) {
          const int dir = panel_drag_acc_ > 0 ? 1 : -1;
          panel_drag_acc_ -= float(dir) * step;
          // The panel may have been replaced by the last step: only the row it started on.
          if (!inspector_open() || inspector_sel_ >= int(inspector_fields_.size())) return;
          auto adjust = inspector_fields_[size_t(inspector_sel_)].adjust;
          if (!adjust) return;
          adjust(dir, false);
          invalidate_all();
        }
        return;
      }
      // Through the rows, the way the finger goes: dragged down, the rows above come into view.
      panel_drag_acc_ += float(g.dy);
      const float row_h = float(inspector_row_h_);
      while (inspector_open() && std::abs(panel_drag_acc_) >= row_h) {
        const int dir = panel_drag_acc_ > 0 ? -1 : 1;
        panel_drag_acc_ += float(dir) * row_h;
        step_inspector(dir);
      }
      return;
    }
    case Gesture::DragEnd:
    case Gesture::Cancel:
      panel_drag_ = PanelDrag::Off;
      return;
    case Gesture::Wheel: {
      if (!inspector_rect_.contains(g.x, g.y)) return;
      wheel_acc_ += g.wheel_y;
      while (inspector_open() && std::abs(wheel_acc_) >= 1) {
        const int dir = wheel_acc_ > 0 ? -1 : 1;  // the wheel turned away: up the list
        wheel_acc_ += float(dir);
        step_inspector(dir);
      }
      return;
    }
    case Gesture::LongPress:
    case Gesture::Pinch:
      return;
  }
}

void App::pointer_quit_menu(const Gesture& g) {
  if (g.kind != Gesture::Press) return;
  if (g.button == 2 || !quit_menu_rect().contains(g.x, g.y)) return close_quit_menu();
  for (int i = 0; i < int(quit_items_.size()); ++i) {
    if (quit_item_rect(i).contains(g.x, g.y)) {
      quit_sel_ = i;
      do_quit_item();
      return;
    }
  }
}

}  // namespace twill
