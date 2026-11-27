// The arrangement (design §7): tracks down, bars across, clips as blocks.
//
//   D-pad              move (left of bar 1 is the track's header)
//   A                  on empty bars: new one-bar clip; on a clip: select it
//   A held + L/R       on a clip: make it longer or shorter, by bars; on a header: change engine
//   B                  delete the clip
//   B held + L/R       move the clip along the track
//   Y                  duplicate the clip (the copy plays the same clip: edit one, both change)
//   X                  on a clip: open it in the piano roll; on a header: track settings;
//                      on an empty bar: project settings (tempo, key, scale; saving and loading)
//   L/R held + d-pad   zoom (left/right), page through tracks (up/down)
//
// The pointer (App.h, "The pointer"), where there is one:
//   press              on a clip: select it; on a header: select the track; on the ruler: the
//                      cursor there (and playback, if playing); on an empty bar: a new one-bar clip
//                      (a finger: on its tap, since its drag may be a scroll)
//   double press       on a clip: open it in the piano roll; on a header: the track's settings
//   drag               a clip: along its track; its right end: its length; from an empty bar (a
//                      mouse): the new clip's length; a finger elsewhere: scroll
//   right-click, or a finger's long press: the clip's menu (open, duplicate, copy, length,
//                      delete); the track's settings; on an empty bar, a menu (new, paste, main menu)
//   wheel, two fingers scroll (Shift + wheel: along the bars); a pinch zooms
#pragma once
#include <vector>

#include "app/App.h"

namespace twill {

class ArrangementView {
 public:
  explicit ArrangementView(App& app) : app_(app) {}

  void handle_input();
  void tick();  // per frame: playhead
  void draw(Canvas& c);
  std::vector<Hint> hints() const;
  Tick cursor_song_position() const { return std::max(0, cursor_bar_) * kBar; }
  void copy();   // the clip under the cursor
  void paste();  // an independent copy at the cursor, on any track (Y makes a linked one)
  void open_main_menu();  // see App::kMainMenuTitle; call App::open_main_menu()
  void open_track_settings(int track, int select_row = 1);  // opens on Engine (row 1), under Name

  // A global chord used L or R (L/R + Start): releasing it then does nothing here.

  void pointer(const Gesture& g);
  // Where things are on screen, for the pointer's tests (empty when not shown).
  Rect bar_rect(int track, int bar) const;
  Rect header_rect(int track) const;
  Rect ruler_rect() const { return layout().ruler; }
  int cursor_bar() const { return cursor_bar_; }
  int top_track() const { return top_track_; }
  int scroll_bar() const { return scroll_bar_; }
  int bars_per_cell() const { return bars_per_cell_; }

 private:
  static constexpr Tick kBar = 4 * kTicksPerQuarter;
  struct Layout {
    Rect ruler, headers, grid;
    int row_h, bar_w;  // pixels
  };
  Layout layout() const;
  int bar_x(const Layout& l, double bar) const;
  // The placement covering the cursor's bar on the cursor's track, or -1.
  int placement_at(int track, int bar) const;
  void clamp_scroll(const Layout& l);
  void follow_cursor();  // the roll opens the clip under the cursor
  bool duplicate(int track, int pi);  // Y: a linked copy after the clip; false if there's no room
  void remove(int track, int pi);
  // The pointer: the track (-1: none) and bar under a point; menus for a right-click or long press.
  int track_at(const Layout& l, int y) const;
  int bar_at(const Layout& l, int x) const;
  void scroll_by(const Layout& l, float tracks, float bars);  // the view only; the cursor stays
  void open_clip_menu(int track, Tick start);
  void open_bar_menu(int track, int bar);

  App& app_;
  int cursor_bar_ = 0;  // -1: the track header column
  int scroll_bar_ = 0, top_track_ = 0;
  int bars_per_cell_ = 1;
  bool a_used_ = false, b_used_ = false;
  int last_playhead_x_ = -1;

  // The pointer's drag: what it holds (the placement by its start, as it is now), and where from.
  enum class Drag { None, Move, Resize, Scroll } drag_ = Drag::None;
  bool grabbed_ = false, grabbed_end_ = false, pressed_empty_ = false, pointer_group_ = false;
  int drag_track_ = -1;
  Tick drag_start_ = 0, orig_start_ = 0, orig_length_ = 0;
  float scroll_tracks_ = 0, scroll_bars_ = 0, pinch_ = 0;
};

}  // namespace twill
