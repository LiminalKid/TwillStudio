// The application: owns the project, its undo history and the engine; routes buttons to the
// current view; draws the top bar, the view and the hint bar; autosaves.
//
// Global buttons (design §7): Select + left/right switches view, Select + B undoes, Select + A
// redoes, Select + up/down moves between notes and lanes in the roll, Select + X arms recording
// from MIDI, Start plays/stops, Select + Start plays from the cursor, Menu + Start quits.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app/ClockFollower.h"
#include "app/InputTracker.h"
#include "app/Theme.h"
#include "app/MidiSender.h"
#include "engine/Engine.h"
#include "engine/PlaitsVoice.h"
#include "gfx/Canvas.h"
#include "model/Edit.h"
#include "platform/Platform.h"

namespace twill {

class ArrangementView;
class InstrumentView;
class MixerView;
class PianoRollView;

// A list of settings shown in a panel on the right; up/down picks one, left/right changes it.
struct InspectorField {
  std::string label;
  std::function<std::string()> value;
  std::function<void(int step, bool coarse)> adjust;  // step is -1 or +1; may be null
  std::function<void()> press = nullptr;               // A; null if A does nothing
  bool heading = false;  // a section title: drawn as one, and the selection passes over it
  bool buttons = false;  // the label is a button combination ("Sel + X"), drawn as keycaps
};

struct Hint {
  const char* keys;
  const char* action;
};

class App {
 public:
  enum class View { Arrangement, PianoRoll, Instrument, Mixer };
  static constexpr const char* kAutosaveName = "autosave.twill";

  explicit App(Platform& platform);
  ~App();

  bool start();  // loads the autosave (or the demo song) and opens audio
  void stop();   // saves and closes audio
  // One frame: input, engine hand-off, drawing. Returns false when the app should quit.
  bool frame(const InputState& input, Canvas& canvas);

  // For the views.
  Platform& platform() { return p_; }
  Project& project() { return project_; }
  EditHistory& history() { return history_; }
  Engine& engine() { return *engine_; }
  const Theme& theme() const { return theme_; }
  // A track's own colour (it keeps it when moved), from the theme's set.
  Color track_colour(int track) const {
    return track >= 0 && track < int(project_.tracks.size()) ? theme_.track(int(project_.tracks[size_t(track)].colour))
                                                              : theme_.track(track);
  }
  // App settings, kept in settings.txt beside the projects (not in a project, not undoable).
  bool dark_mode() const { return theme_.dark; }
  void set_dark_mode(bool on);
  // New projects get a made-up two-word name ("Velvet Harbour") instead of "Untitled".
  bool auto_names() const { return auto_names_; }
  void set_auto_names(bool on);
  std::string fresh_song_name();  // one no saved project has
  const InputTracker& input() const { return input_; }
  float u(float units) const { return units * scale_; }
  float scale_value() const { return scale_; }

  int track() const { return track_; }
  // Clipboards: notes (starts relative to the first) from the roll, and a clip from the arrangement.
  std::vector<Note>& note_clipboard() { return note_clipboard_; }
  Clip& clip_clipboard() { return clip_clipboard_; }  // empty (id 0) until something's copied
  void set_track(int t);
  // The clip the piano roll shows for the current track, and where it's placed (null if none).
  const Placement* open_placement() const;
  Clip* open_clip();
  const Clip* open_clip() const { return const_cast<App*>(this)->open_clip(); }
  void open(int track, const Placement& pl);  // makes `pl` the track's open clip
  void show(View v);
  // Moves through the views in order (arrangement, roll, sound, mixer), wrapping round.
  void step_view(int by);
  // Opens the settings panel, or puts it away if it is open.
  void toggle_settings();
  // Arms or disarms recording from MIDI (the settings panel's "Record from MIDI").
  void toggle_recording();
  // A panel listing every button combination.
  void open_controls();
  View view() const { return view_; }

  void audition(int track, int pitch);
  // Y in the views with nothing to select: a note of the track's sound (a kit's first slot, a
  // sample's own note, else middle C).
  void listen(int track);
  // The track's settings panel (X on its header in the arrangement, X in the sound view).
  void open_track_settings(int track);
  // A track's sound, as one list: the macro oscillator's engines, then the sampler, then MIDI out.
  // "Engine" in the app means a place in this list.
  static constexpr int kNumSounds = PlaitsVoice::kNumEngines + 2;
  static int sound_index(const Track& t);
  static const char* sound_name(const Track& t);
  void step_sound(int track, int step);  // one undo step; plays a note to hear it
  void invalidate(const Rect& r);
  void invalidate_all();
  Rect content_rect() const { return content_; }
  void open_inspector(std::string title, std::vector<InspectorField> fields);
  bool inspector_open() const { return !inspector_fields_.empty(); }
  const std::string& inspector_title() const { return inspector_title_; }
  int inspector_scroll() const { return inspector_scroll_; }  // first row shown
  void close_inspector();
  int inspector_selection() const { return inspector_sel_; }
  void select_inspector_row(int row);  // scrolls it into view
  void message(const std::string& text);  // shown briefly in the hint bar
  // Adds the project file actions (save, load, new, render) to a settings panel's fields.
  void add_project_fields(std::vector<InspectorField>& fields);
  void open_about();  // version, commit and build date
  // Project files, in projects/ in the data folder.
  bool save_project_file();
  bool load_project_file(const std::string& name);
  void new_project();
  bool render_song_to_wav();
  bool has_unsaved_changes();  // compared with its file in projects/ (true if it has none)

  // Samples, by path (relative to the data folder unless absolute), loaded once and kept while the
  // project uses them: voices and snapshots point into them. Null (with a reason) if it can't be
  // loaded. One the project stops using is freed once the engine has let go of it (see
  // free_unused_samples()); using it again loads it again.
  std::shared_ptr<const SampleData> load_sample(const std::string& path, std::string* error = nullptr);
  size_t loaded_samples() const { return samples_.size(); }
  void free_unused_samples();  // every frame; checks twice a second

 private:
  void handle_global();
  void handle_inspector();
  void sync_engine();
  void play_from(Tick song_position);
  void autosave(bool force);
  void layout(const Canvas& c);
  void draw(Canvas& c);
  void draw_top_bar(Canvas& c);
  void draw_hint_bar(Canvas& c);
  void draw_inspector(Canvas& c);
  // A panel's sheet, shadow and title; returns the y below the title's rule.
  int draw_panel_frame(Canvas& c, const char* title);
  void validate_open_clips();
  void replace_project(Project p);
  // Asks for a second press within a few seconds before discarding unsaved work.
  bool confirm_discard(const char* what);
  void handle_name_edit();
  void handle_midi();
  // Following an external clock: the engine's tempo, nudged to keep it in step with the master.
  void follow_clock();
  int64_t midi_now_us() const;  // the clock incoming MIDI is stamped with
  ClockFollower clock_in_;
  double tempo_override_ = 0;  // sent to the engine; 0: the project's
  double phase_error_ = 0;     // beats the engine is ahead of the master, smoothed
  int output_latency_ = 0;     // frames from rendering to the speaker
  int block_frames_ = 256;
  void load_settings();
  bool auto_names_ = true;
  uint32_t names_made_ = 0;
  void save_settings();
  // Recording from MIDI into the open clip (Select + X arms it).
  bool recording_ = false;
  struct HeldNote {
    uint8_t pitch;
    uint32_t note_id;
    Tick start;  // clip time
  };
  std::vector<HeldNote> held_notes_;
  void draw_name_edit(Canvas& c);

  Platform& p_;
  Theme theme_;
  Project project_;
  EditHistory history_{project_};
  std::unique_ptr<Engine> engine_;
  std::unique_ptr<MidiSender> midi_sender_;  // after engine_: it reads the engine
  std::unique_ptr<ArrangementView> arrangement_;
  std::unique_ptr<PianoRollView> roll_;
  std::unique_ptr<InstrumentView> instrument_;
  std::unique_ptr<MixerView> mixer_;
  std::map<std::string, std::shared_ptr<const SampleData>> samples_;
  std::map<std::string, std::string> sample_errors_;  // so a missing file isn't retried every frame
  // Loaded samples the project no longer uses, waiting for the engine to let go of them: the
  // snapshot it must be playing, and the callback count it must pass (two on from when nothing else
  // held the sample, so a preview that was playing it has stopped).
  struct UnusedSample {
    uint64_t snapshot = 0, callbacks = 0;
  };
  std::map<std::string, UnusedSample> unused_samples_;
  uint64_t unused_checked_us_ = 0;
  InputTracker input_;
  float scale_ = 1;
  View view_ = View::Arrangement;
  int track_ = 0;
  std::vector<Placement> open_;  // per track: the placement the roll shows (clip_id 0: none)

  uint64_t synced_revision_ = ~0ull;
  uint64_t transport_changes_ = 0, synced_transport_ = ~0ull, snapshot_counter_ = 0;
  uint64_t saved_revision_ = 0, last_edit_us_ = 0;
  uint64_t last_revision_seen_ = 0;
  bool quit_ = false;

  // Layout and drawing.
  Size size_;
  Rect top_bar_, content_, hint_bar_, inspector_rect_;
  std::vector<Rect> dirty_;
  std::string top_text_cache_, hint_cache_;
  std::string message_;
  uint64_t message_until_us_ = 0;

  // Name editing (a character at a time, with the d-pad).
  bool name_edit_ = false;
  std::string name_draft_;
  int name_cursor_ = 0;
  // Discard confirmations.
  std::string confirm_what_;
  uint64_t confirm_until_us_ = 0;

  // Inspector.
  std::string inspector_title_;
  std::vector<InspectorField> inspector_fields_;
  // Notes from a MIDI keyboard passed on to a MIDI Out track: where each one went, for its note off.
  struct LiveOut {
    int8_t channel = -1;
    int32_t source = -1;
  };
  LiveOut live_out_[128];
  int inspector_sel_ = 0;
  uint32_t voices_stolen_seen_ = 0;
  uint64_t voices_full_until_us_ = 0;  // "Voices full" shows in the top bar until then
  int inspector_scroll_ = 0;
  bool menu_used_ = false;
  bool menu_panel_ = false;  // the panel open came from tapping Menu: another tap closes it  // another button went with Menu (Menu + Start quits), so no panel
  std::vector<Note> note_clipboard_;
  Clip clip_clipboard_;  // first row shown, when the fields don't all fit
};

}  // namespace twill
