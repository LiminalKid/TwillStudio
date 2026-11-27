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
#include "app/Pointer.h"

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

  // The open clip's panel: its loop length, the grid, key and scale... (Menu in the roll).
  void open_clip_settings();

  // The pointer (App.h, "The pointer"): a tap on an empty cell adds a note, on a note selects it
  // (Shift: adds it to the selection); a double tap or a right-click deletes it; dragging a note
  // moves it, dragging its right end changes its length, and a drag from an empty cell draws a
  // note that long. Shift or the right button dragged over empty cells selects them (a finger: a
  // long press, then a drag; otherwise a finger's drag scrolls). A key sounds while held, sliding
  // from key to key; the ruler moves the cursor (and playback there, while playing); the lane strip takes a tap or a drag for its values. The wheel and two
  // fingers scroll, Shift + wheel across; a pinch zooms the grid.
  void pointer(const Gesture& g);
  void let_go();  // a key the pointer holds down: let go of (leaving the roll)
  int key_held() const { return key_held_; }
  // For tests: where a cell is drawn, a note, and the lane strip (empty when not shown).
  Rect cell_rect(Tick t, int pitch) const;
  Rect lane_rect() const { return layout().lane; }
  Rect keys_rect() const { return layout().keys; }
  const std::vector<uint32_t>& selection() const { return selection_; }
  int top_row() const { return top_row_; }
  Tick scroll() const { return scroll_; }
  Tick grid_ticks() const { return snap(); }
  Tick cursor() const { return cursor_; }
  int cursor_pitch() const { return cursor_pitch_; }
  Rect ruler_rect() const { return layout().ruler; }

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
  void open_settings();  // X: the note panel for the selection (or the note under the cursor), else the clip's
  void audition(int pitch) { app_.audition(app_.track(), pitch); }
  void lane_input();
  // The pointer: the row (-1: none) and tick under a point; the note under it, and whether that's
  // near its right end (to change its length).
  int row_at(const Layout& l, int y) const;
  Tick tick_at(const Layout& l, int x) const;  // exact, not on the grid
  const Note* note_at(const Layout& l, int x, int y, bool touch, bool& at_end) const;
  void pointer_tap(const Layout& l, const Gesture& g);
  void pointer_lane(const Layout& l, int x, int y, bool remove);
  void scroll_by(const Layout& l, float rows, float cells);  // the view only; the cursor stays
  void select_area(Tick t0, Tick t1, int pitch0, int pitch1, bool add);
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
  // A drag going on: what it does, the notes as they were when it began, and what it last applied.
  enum class Drag { None, Move, Resize, Area, Scroll, Lane, Keys } drag_ = Drag::None;
  std::vector<Note> drag_notes_;
  uint32_t drag_note_ = 0;  // the note it took hold of
  Tick drag_dt_ = 0;
  int drag_rows_ = 0;
  bool drag_add_ = false;  // Shift: an area adds to the selection
  bool area_ = false;      // the area is drawn (y_anchor_* to the cursor)
  float scroll_rows_ = 0, scroll_cells_ = 0, pinch_ = 0;  // what's left over, below a row or a cell
  uint32_t tap_added_ = 0;  // the note the last press added: a double click's second leaves it
  bool pointer_group_ = false;  // an undo group the pointer began, ended when it's let go of
  bool pressed_empty_ = false;  // a finger went down on an empty cell: its tap adds a note
  bool pressed_lane_ = false;   // the press was in the lane strip
  int key_held_ = -1, key_track_ = -1;  // a key held down with the pointer: sounding, like a MIDI key
};

}  // namespace twill
