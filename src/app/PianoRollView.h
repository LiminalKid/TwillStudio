// The piano roll (design §7): one clip of the current track; time across, pitch down.
//
//   D-pad              move the cursor by one grid step / one row
//   A                  add a note (last length used), or select the note under the cursor
//   A held + L/R       shorten / lengthen the selected notes by a grid step
//   A held + U/D       transpose them by a row (with R held: an octave)
//   B                  delete the note under the cursor (or the selection, if it's selected)
//   B held + d-pad     move the selected notes
//   Y held + d-pad     select a rectangle; Y alone toggles the note under the cursor
//   X                  settings for the selected notes, or for the clip and grid
//   L/R                previous / next track
//   L/R held + d-pad   zoom (grid step) left/right; page up/down
//
// Zooming changes the grid step and keeps cells the same width, so the cursor always moves one
// cell. In scale mode ("fold") only the scale's notes get rows (plus any notes already off-scale).
#pragma once
#include <vector>

#include "app/App.h"

namespace twill {

class PianoRollView {
 public:
  explicit PianoRollView(App& app) : app_(app) {}

  void handle_input();
  void tick();  // per frame: playhead
  void draw(Canvas& c);
  std::vector<Hint> hints() const;
  Tick cursor_song_position() const;
  // Select + Down / Up: the cursor works in the lane strip under the roll, or on the notes.
  void set_lane_focus(bool on);
  void copy();   // the selected notes, or the one under the cursor
  void select_all();  // every note in the open clip
  void paste();  // at the cursor, keeping pitches and spacing; the pasted notes end up selected

  // A global chord used L or R (L/R + Start): releasing it then does nothing here.

 private:
  struct Layout {
    Rect ruler, keys, grid;
    Rect lane, lane_label;  // empty when no lane is shown
    int row_h, cell_w;      // pixels; a cell is one grid step
  };
  Layout layout() const;
  Tick snap() const;
  int tick_x(const Layout& l, Tick t) const;
  void rebuild_rows();
  int row_of(int pitch) const;  // -1 if the pitch has no row
  const Note* note_under_cursor() const;
  // The notes an A/B gesture acts on: the selection, if it includes the note under the cursor
  // (or there's no note under it); otherwise just the note under the cursor.
  std::vector<uint32_t> targets();
  int shifted_pitch(int pitch, int rows, bool octave) const;
  void keep_cursor_visible(const Layout& l);
  void open_settings();
  void audition(int pitch) { app_.audition(app_.track(), pitch); }
  void lane_input();
  void draw_lane(Canvas& c, const Layout& l, const Clip& clip);
  std::string lane_name() const;
  // The lane's range, and its value at a tick when it has no points (the track's setting).
  void lane_range(float& lo, float& hi) const;

  App& app_;
  Tick cursor_ = 0, scroll_ = 0;
  int cursor_pitch_ = 60, top_row_ = 0;
  int snap_index_ = 6;  // 1/16; see kSnaps in the .cpp
  bool fold_ = false;
  Tick last_length_ = kTicksPerQuarter / 4;
  std::vector<uint32_t> selection_;
  std::vector<int> rows_;  // pitches, top row first
  bool a_used_ = false, b_used_ = false, y_used_ = false;
  bool y_selected_all_ = false;  // Y + A selected everything: releasing Y leaves it so
  Tick y_anchor_tick_ = 0;
  int y_anchor_pitch_ = 60;
  int last_playhead_x_ = -1;
  uint32_t shown_clip_ = 0;
  // Lane strip: -1 none, 0 velocity, otherwise the ParamId it automates.
  int lane_ = 0;
  bool lane_focus_ = false;
  bool lane_group_open_ = false;
};

}  // namespace twill
