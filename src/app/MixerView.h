// The mixer (design §7, "Mixer"): a strip per track (meter, level, pan, the three sends, mute,
// solo) and a master strip (level and a three-band EQ).
//
//   D-pad              move between strips and rows
//   A held + d-pad     change the value under the cursor (R held: bigger steps)
//   A                  on Mute or Solo: switch it
//   B                  reset the value to its default
//   X                  effect settings (reverb, delay, chorus)
//
// The pointer (App.h, "The pointer"), where there is one: a press on a slider sets it there, and a
// drag goes on setting it; a right-click or a double press puts it back to its default; a press
// on M or S switches it; the wheel over a slider moves it a little (Shift: more).
#pragma once
#include <vector>

#include "app/App.h"

namespace twill {

class MixerView {
 public:
  explicit MixerView(App& app) : app_(app) {}

  void handle_input();
  void tick();  // per frame: meters
  void draw(Canvas& c);
  std::vector<Hint> hints() const;

  void pointer(const Gesture& g);
  Rect cell_rect(int strip, int row) const;  // for the pointer's tests
  int strip() const { return strip_; }
  int row() const { return row_; }
  static constexpr int kLevelRow = 0, kPanRow = 1, kMuteRow = 5, kSoloRow = 6;

 private:
  enum Row { kLevel, kPan, kChorus, kDelay, kReverb, kMute, kSolo, kNumRows };
  struct Layout {
    Rect labels;
    int strip_w, x0;
    int name_h, level_h, row_h, top;
  };
  Layout layout() const;
  int strips() const { return int(app_.project().tracks.size()) + 1; }  // tracks, then master
  bool is_master(int strip) const { return strip == strips() - 1; }
  ParamId param_at(int strip, int row) const;  // kParamNone for mute/solo and empty cells
  Rect strip_rect(const Layout& l, int strip) const;
  void open_effects();
  int row_y(const Layout& l, int row) const;
  int row_height(const Layout& l, int row) const;
  // The pointer: the strip and row under a point (false if none); a slider's value at a point.
  bool cell_at(const Layout& l, int x, int y, int& strip, int& row) const;
  float value_at(const Layout& l, int strip, int row, int x, int y) const;
  void set_value(int strip, int row, float v);
  void show_value();  // the value under the cursor, in words, in the hint bar

  App& app_;
  int strip_ = 0, row_ = kLevel;
  bool a_used_ = false;
  std::vector<float> meters_;  // shown levels, decaying
  float master_meter_ = 0;
  float comp_shown_ = -1;  // the master compressor's gain reduction as last drawn (dB); -1 off
  bool sliding_ = false, pointer_group_ = false;  // a slider held with the pointer
  float wheel_acc_ = 0;
};

}  // namespace twill
