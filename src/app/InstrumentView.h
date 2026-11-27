// The sound of the current track (design §7, "Instrument"): a list of settings on the left, and a
// picture on the right (the engine for the macro oscillator; the waveform, the kit's slots or a
// multisample's zones for the sampler; the envelope while its settings are chosen). Also the
// sample browser.
//
//   Up/Down        choose a setting           Left/Right   change it (R held: bigger steps)
//   A              on a sample: browse; on an action: do it
//   B              reset the setting to its default
//   Y              play the sound
//   L/R            previous / next track
//
// Browser: Up/Down choose, A open folder or pick, B up a folder (or close), Y preview, X close.
//
// Recorder (design §6, "Recording samples"): the Record row under Sample (or Slot sample) opens it
// in place of the settings. Ready: Up/Down choose, Left/Right change, A record (or listen for the
// first sound), B or X close. Listening: A record now, B cancel. Recording: A stop, B cancel.
// Keep?: A keep, Y play, B record again, X discard.
//
// The pointer (App.h, "The pointer"), where there is one: the settings work as a panel's rows do (a
// press selects one; a press on the selected one does its job, or a click steps its value, Shift
// the other way; a drag across changes it); a press or drag on a setting's bar sets it there; a
// right-click or a double click resets it (B). A press on the picture plays the sound (Y), on a
// kit's pad selects that pad and plays it. In the browser a press selects (an audio file is heard
// as well) and a press on the selection opens or picks it; a press on the folder's name goes up;
// a right-click or a long press closes it. The wheel, a drag up and down and two fingers go
// through the list.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/App.h"
#include "app/Recorder.h"

namespace twill {

class InstrumentView {
 public:
  explicit InstrumentView(App& app) : app_(app) {}

  void handle_input();
  void tick();  // the zone map follows the zones sounding
  void draw(Canvas& c);
  std::vector<Hint> hints() const;
#if TWILL_PLUGINS
  bool browsing() const { return browser_open_ || plugins_open_; }
#else
  bool browsing() const { return browser_open_; }
#endif

  // A global chord used L or R (L/R + Start): releasing it then does nothing here.

  void pointer(const Gesture& g);
  // Where things are on screen, for the pointer's tests (empty when not shown).
  Rect row_rect(int row);
  Rect value_bar_rect(int row);
  Rect entry_rect(int entry) const;  // the browser's (or the plugin list's)
  Rect picture_rect() const;
  Rect kit_pad_rect(int slot) const;
  int selection() const { return sel_; }
  int browser_selection() const { return bsel_; }
  int kit_slot() const { return kit_slot_; }
  int find_row(const char* label);  // -1 if there's none

  // The sample recorder.
  bool recorder_open() const { return recorder_open_; }
  Recorder* recorder() { return recorder_.get(); }
  bool recorder_warning() const { return rec_warning_; }
  void leave();  // the view is left: the recorder closes

 private:
  struct Row {
    std::string label;
    std::function<std::string()> value;
    std::function<float()> fraction;            // for the bar; negative: no bar
    std::function<void(int step, bool coarse)> adjust;
    std::function<void()> press;                // A; null if A does nothing here
    std::function<void()> reset;                // B; null if nothing to reset
    ParamId param = kParamNone;                 // the setting it changes, if it's one
    bool dim = false;                           // shown, but not to be changed now
  };
  std::vector<Row> rows();
  std::vector<Row> current_rows() { return recorder_open_ ? recorder_rows() : rows(); }
  Row param_row(ParamId id, const char* label = nullptr);
  enum class Browse { Samples, Presets };  // what the file browser lists besides folders
  void open_browser(std::function<void(const std::string&)> on_pick, const std::string& start,
                    Browse what = Browse::Samples);
  void browser_input();
  void close_browser();
  void browser_go(const std::string& dir, const std::string& select);
  void browser_up();  // to the parent, with the folder it came from selected
  void browser_choose(int entry, bool preview);  // A (or, preview: Y) on an entry
  // The pointer.
  Rect list_rect() const;
  Rect bar_in(const Rect& row) const;  // a row's value bar
  int row_at(int count, int x, int y) const;
  int entry_at(int x, int y) const;
  void pointer_list(const Gesture& g);
  void pointer_browser(const Gesture& g);
  void step_selection(int dir);
  void set_from_bar(const Row& row, const Rect& bar, int x);
  void draw_browser(Canvas& c, const Rect& r);
  void draw_waveform(Canvas& c, const Rect& r, const SampleData& s, const Track& t);
  void draw_kit(Canvas& c, const Rect& r, const Track& t);
  void draw_macro(Canvas& c, const Rect& r, const Track& t);
  void draw_zones(Canvas& c, const Rect& r, const Track& t);
  // While the amp envelope's settings are chosen: its shape, the selected stage marked.
  void draw_adsr(Canvas& c, const Rect& r, const Track& t, ParamId selected);
  void draw_lpg(Canvas& c, const Rect& r, const Track& t, ParamId selected);
  int audition_note() const;

  // The recorder: opened for the track's sample or a kit slot (-1: the sample).
  void open_recorder(int slot);
  void close_recorder();
  bool reopen_input();  // the chosen input, again (after a change of input)
  std::vector<Row> recorder_rows();
  void recorder_input();
  void recorder_go();     // A in Ready, Listening and Recording
  void recorder_keep();   // A in Keep?: saved, and into the track
  void recorder_play();   // Y in Keep?
  void draw_recorder(Canvas& c, const Rect& pic);
  std::string take_name() const;  // "<track> rec <n>", the first n not used
  std::string chosen_input() const;  // the input to open: the one set, if it's there; else the default
  bool input_is_mic() const;
  bool recorder_open_ = false;
  std::unique_ptr<Recorder> recorder_;
  int rec_track_ = -1, rec_slot_ = -1;
  int saved_sel_ = 0, saved_top_ = 0;
  bool rec_warning_ = false, rec_warned_ = false;  // the speakers card: up now; shown already
  Recorder::State rec_drawn_ = Recorder::State::Closed;
  std::string rec_error_;
  std::shared_ptr<const SampleData> rec_preview_;
  std::vector<float> rec_bins_;  // the take's peaks, 10 ms each, for drawing as it grows

  // The pointer: what the press found, and what a drag does.
  bool press_was_selected_ = false, press_acted_ = false, tap_acted_ = false, pointer_group_ = false;
  enum class Slide { Off, None, Adjust, Value, Scroll } slide_ = Slide::Off;
  int slide_row_ = -1;
  float slide_acc_ = 0, wheel_acc_ = 0;

  App& app_;
  int sel_ = 0, top_ = 0;
  int kit_slot_ = 0;
  std::vector<int> sounding_;  // the zones of this track's multisample sounding, as last drawn

  // Browser.
  bool browser_open_ = false;
  Browse browse_ = Browse::Samples;
  std::string dir_;                      // absolute, ends with '/'
  std::vector<Platform::DirEntry> entries_;  // starts with ".." ("Up a folder") below the root
  bool list_browser_dir(const std::string& dir, const std::string& select);  // false: unreadable
  int bsel_ = 0, btop_ = 0;
  std::function<void(const std::string&)> on_pick_;
  std::shared_ptr<const SampleData> preview_;

#if TWILL_PLUGINS
  // The plugin list (a Plugin track's "Plugin" row): what the platform has installed.
  // Up/Down choose (R held: by 10), A picks, B or X close.
  bool plugins_open_ = false;
  std::vector<PluginInfo> plugin_list_;
  int psel_ = 0, ptop_ = 0;
  void open_plugins();
  void plugins_input();
  void draw_plugins(Canvas& c, const Rect& r);
  void draw_plugin(Canvas& c, const Rect& r, const Track& t);
#endif

  // Waveform overview cache.
  const SampleData* wave_for_ = nullptr;
  std::shared_ptr<const SampleData> wave_hold_;  // the one shown, kept so its address stays its own
  int wave_width_ = 0;
  std::vector<float> wave_peaks_;  // per column: max |l|,|r|
};

}  // namespace twill
