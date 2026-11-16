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
  void open_project_settings();  // tempo, key, scale, files, MIDI clock (Menu, from any view)

  // A global chord used L or R (L/R + Start): releasing it then does nothing here.

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
  void open_track_settings(int track, int select_row = 0);  // select_row: after reopening
  void clamp_scroll(const Layout& l);

  App& app_;
  int cursor_bar_ = 0;  // -1: the track header column
  int scroll_bar_ = 0, top_track_ = 0;
  int bars_per_cell_ = 1;
  bool a_used_ = false, b_used_ = false;
  int last_playhead_x_ = -1;
};

}  // namespace twill
