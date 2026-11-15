// The sound of the current track (design §7, "Instrument"): a list of settings on the left, and a
// picture on the right (the engine for the macro oscillator; the waveform, or the kit's slots,
// for the sampler). Also the sample browser.
//
//   Up/Down        choose a setting           Left/Right   change it (R held: bigger steps)
//   A              on a sample: browse; on an action: do it
//   B              reset the setting to its default
//   Y              play the sound
//   L/R            previous / next track
//
// Browser: Up/Down choose, A open folder or pick, B up a folder (or close), Y preview, X close.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/App.h"

namespace twill {

class InstrumentView {
 public:
  explicit InstrumentView(App& app) : app_(app) {}

  void handle_input();
  void tick() {}
  void draw(Canvas& c);
  std::vector<Hint> hints() const;
  bool browsing() const { return browser_open_; }

  // A global chord used L or R (L/R + Start): releasing it then does nothing here.
  void consume_lr() { lr_used_ = true; }

 private:
  struct Row {
    std::string label;
    std::function<std::string()> value;
    std::function<float()> fraction;            // for the bar; negative: no bar
    std::function<void(int step, bool coarse)> adjust;
    std::function<void()> press;                // A; null if A does nothing here
    std::function<void()> reset;                // B; null if nothing to reset
  };
  std::vector<Row> rows();
  Row param_row(ParamId id, const char* label = nullptr);
  void open_browser(std::function<void(const std::string&)> on_pick, const std::string& start);
  void browser_input();
  void draw_browser(Canvas& c, const Rect& r);
  void draw_waveform(Canvas& c, const Rect& r, const SampleData& s, const Track& t);
  void draw_kit(Canvas& c, const Rect& r, const Track& t);
  void draw_macro(Canvas& c, const Rect& r, const Track& t);
  int audition_note() const;

  App& app_;
  int sel_ = 0, top_ = 0;
  int kit_slot_ = 0;
  bool lr_used_ = false;

  // Browser.
  bool browser_open_ = false;
  std::string dir_;                      // absolute, ends with '/'
  std::vector<Platform::DirEntry> entries_;  // starts with ".." ("Up a folder") below the root
  bool list_browser_dir(const std::string& dir, const std::string& select);  // false: unreadable
  int bsel_ = 0, btop_ = 0;
  std::function<void(const std::string&)> on_pick_;
  std::shared_ptr<const SampleData> preview_;

  // Waveform overview cache.
  const SampleData* wave_for_ = nullptr;
  std::shared_ptr<const SampleData> wave_hold_;  // the one shown, kept so its address stays its own
  int wave_width_ = 0;
  std::vector<float> wave_peaks_;  // per column: max |l|,|r|
};

}  // namespace twill
