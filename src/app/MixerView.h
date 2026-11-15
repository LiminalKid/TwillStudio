// The mixer (design §7, "Mixer"): a strip per track (meter, level, pan, the three sends, mute,
// solo) and a master strip (level and a three-band EQ).
//
//   D-pad              move between strips and rows
//   A held + d-pad     change the value under the cursor (R held: bigger steps)
//   A                  on Mute or Solo: switch it
//   B                  reset the value to its default
//   X                  effect settings (reverb, delay, chorus)
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

  App& app_;
  int strip_ = 0, row_ = kLevel;
  bool a_used_ = false;
  std::vector<float> meters_;  // shown levels, decaying
  float master_meter_ = 0;
  float comp_shown_ = -1;  // the master compressor's gain reduction as last drawn (dB); -1 off
};

}  // namespace twill
