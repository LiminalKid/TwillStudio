#include "app/App.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "app/ArrangementView.h"
#include "app/DemoSong.h"
#include "app/FactorySounds.h"
#include "app/InstrumentView.h"
#include "app/MixerView.h"
#include "app/Paper.h"
#include "app/PianoRollView.h"
#include "app/SongNames.h"
#include "Version.h"
#include "engine/PlaitsVoice.h"
#include "model/ProjectFile.h"

namespace twill {

namespace {
// Sizes, in layout units (x2 on the Brick Pro); see the arrangement's for the reasoning.
constexpr float kTopBarH = 32, kHintBarH = 28, kTopText = 16, kHintText = 13;
constexpr float kPanelW = 250, kPanelTitle = 18, kPanelText = 13, kPanelRowH = 24;
constexpr uint64_t kAutosaveDelayUs = 3000000;  // save 3 s after the last edit
constexpr int kStrip = 32;                      // see App::draw
}  // namespace

App::App(Platform& platform) : p_(platform) {}
App::~App() = default;

bool App::start() {
  scale_ = p_.ui_scale();
  load_settings();
  if (int n = make_factory_sounds(p_)) p_.log("made %d factory sounds", n);
  std::vector<uint8_t> bytes;
  std::string error;
  bool loaded = p_.read_file(kAutosaveName, bytes) &&
                load_project(bytes.data(), bytes.size(), project_, error) && !project_.tracks.empty();
  if (!loaded) {
    if (!error.empty()) p_.log("autosave not loaded: %s", error.c_str());
    project_ = make_demo_song();
  }
  open_.assign(project_.tracks.size(), Placement{});
  validate_open_clips();

  engine_ = std::make_unique<Engine>(p_.max_voices());
  arrangement_ = std::make_unique<ArrangementView>(*this);
  roll_ = std::make_unique<PianoRollView>(*this);
  instrument_ = std::make_unique<InstrumentView>(*this);
  mixer_ = std::make_unique<MixerView>(*this);
  sync_engine();

  bool ok = p_.audio_open(Engine::kSampleRate, 256, [](void* user, float* out, int frames) {
    static_cast<App*>(user)->engine_->render(out, frames);
  }, this);
  AudioInfo ai = p_.audio_info();
  p_.log("audio %s: %d Hz, %d frames per block", ok ? "open" : "FAILED", ai.sample_rate,
         ai.block_frames);
  if (ok && ai.sample_rate != Engine::kSampleRate)
    p_.log("warning: device runs at %d Hz; pitch will be off until resampling exists", ai.sample_rate);
  // MIDI out leaves when the audio around it reaches the speaker: about two blocks after rendering.
  midi_sender_ = std::make_unique<MidiSender>(*engine_, p_.midi());
  // (Plus the master limiter's look-ahead.)
  output_latency_ = 2 * ai.block_frames + Engine::kOutputDelay;
  block_frames_ = std::max(1, ai.block_frames);
  midi_sender_->set_latency_us(int64_t(output_latency_) * 1000000 / std::max(1, ai.sample_rate));
  if (p_.real_time() && p_.midi()) midi_sender_->start_thread();
  saved_revision_ = history_.revision();
  return ok;
}

void App::stop() {
  // For anyone reporting dropouts: the log says how close the audio came to its limit.
  p_.log("audio load: highest %.0f%%, %u blocks late", engine_->load_highest() * 100, unsigned(engine_->late_blocks()));
  autosave(true);
  if (awake_) p_.keep_awake(awake_ = false);
  p_.audio_close();
  midi_sender_.reset();
}

// --- State the views share ------------------------------------------------------------------

void App::set_track(int t) {
  t = std::max(0, std::min(t, int(project_.tracks.size()) - 1));
  if (t == track_) return;
  track_ = t;
  invalidate_all();
}

void App::validate_open_clips() {
  open_.resize(project_.tracks.size());
  for (size_t t = 0; t < project_.tracks.size(); ++t) {
    const auto& arr = project_.tracks[t].arrangement;
    bool still_there = std::find(arr.begin(), arr.end(), open_[t]) != arr.end();
    if (!still_there) {
      // Keep showing the same clip if it's still placed somewhere; else the track's first.
      auto same = std::find_if(arr.begin(), arr.end(),
                               [&](const Placement& p) { return p.clip_id == open_[t].clip_id; });
      open_[t] = same != arr.end() ? *same : arr.empty() ? Placement{} : arr.front();
    }
  }
}

const Placement* App::open_placement() const {
  if (track_ >= int(open_.size()) || open_[size_t(track_)].clip_id == 0) return nullptr;
  return &open_[size_t(track_)];
}

Clip* App::open_clip() {
  const Placement* pl = open_placement();
  return pl ? project_.find_clip(pl->clip_id) : nullptr;
}

void App::open(int track, const Placement& pl) {
  if (track < 0 || track >= int(open_.size())) return;
  open_[size_t(track)] = pl;
  set_track(track);
  invalidate_all();
}

void App::toggle_recording() {
  recording_ = !recording_;
  message(recording_ ? "Recording from MIDI: play to record into the clip" : "Recording off");
  invalidate(top_bar_);
}

void App::toggle_settings() {
  if (inspector_open() && menu_panel_) {
    close_inspector();
  } else {
    arrangement_->open_project_settings();
    menu_panel_ = true;
  }
}

void App::step_view(int by) {
  const View order[] = {View::Arrangement, View::PianoRoll, View::Instrument, View::Mixer};
  const int n = 4;
  int i = 0;
  while (order[i] != view_) ++i;
  show(order[(i + by % n + n) % n]);
}

void App::show(View v) {
  if (v == view_) return;
  view_ = v;
  close_inspector();
  invalidate_all();
}

std::shared_ptr<const SampleData> App::load_sample(const std::string& path, std::string* error) {
  auto it = samples_.find(path);
  if (it != samples_.end()) return it->second;
  auto failed = sample_errors_.find(path);
  if (failed != sample_errors_.end()) {
    if (error) *error = failed->second;
    return nullptr;
  }
  std::vector<uint8_t> bytes;
  std::string err;
  std::shared_ptr<const SampleData> s;
  if (!p_.read_file(path.c_str(), bytes)) err = "Can't read " + path;
  else s = decode_sample(bytes.data(), bytes.size(), path.substr(path.find_last_of('/') + 1), err);
  if (!s) {
    sample_errors_[path] = err;
    if (error) *error = err;
    return nullptr;
  }
  samples_[path] = s;
  return s;
}

void App::free_unused_samples() {
  const uint64_t now = p_.micros();
  if (now - unused_checked_us_ < 500000 && unused_checked_us_ != 0) return;
  unused_checked_us_ = now;
  // In use: anything a track names (whatever its instrument, so switching back doesn't reload).
  // The newest snapshot matches the project here (sync_engine() ran earlier in the frame).
  auto used = [&](const std::string& path) {
    for (const Track& t : project_.tracks) {
      if (t.sampler.sample == path) return true;
      for (const KitSlot& k : t.sampler.kit)
        if (k.sample == path) return true;
    }
    return false;
  };
  const uint64_t callbacks = engine_->callbacks();
  for (auto it = samples_.begin(); it != samples_.end();) {
    if (used(it->first)) {
      unused_samples_.erase(it->first);
      ++it;
      continue;
    }
    auto u = unused_samples_.find(it->first);
    if (u == unused_samples_.end()) {
      unused_samples_[it->first] = UnusedSample{snapshot_counter_, callbacks};
      ++it;
    } else if (it->second.use_count() > 1) {  // held elsewhere (a preview, the waveform)
      u->second.callbacks = callbacks;
      ++it;
    } else if (engine_->playing_revision() >= u->second.snapshot && callbacks >= u->second.callbacks + 2) {
      unused_samples_.erase(u);
      it = samples_.erase(it);
    } else {
      ++it;
    }
  }
}

void App::audition(int track, int pitch) {
  if (!engine_->playing()) engine_->audition(track, uint8_t(std::max(0, std::min(127, pitch))), 0.3f);
}

void App::listen(int track) {
  if (track < 0 || track >= int(project_.tracks.size())) return;
  const Track& t = project_.tracks[size_t(track)];
  int note = 60;
  if (t.instrument == InstrumentType::Sampler) note = t.sampler.mode == SamplerMode::Kit ? kKitFirstNote : t.sampler.root;
  audition(track, note);
}

void App::open_track_settings(int track) { arrangement_->open_track_settings(track); }

void App::message(const std::string& text) {
  message_ = text;
  message_until_us_ = p_.micros() + 2000000;
  invalidate(hint_bar_);
}

namespace {
constexpr const char* kProjectsFolder = "projects";
constexpr const char* kRendersFolder = "renders";
const char kNameChars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string file_safe(const std::string& name) {
  std::string out;
  for (char c : name) out += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ' ') ? c : '_';
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out.empty() ? std::string("Untitled") : out;
}
}  // namespace

void App::open_about() {
  auto row = [](const char* label, std::string value) {
    return InspectorField{label, [value] { return value; }, nullptr};
  };
  std::vector<InspectorField> fields;
  fields.push_back(row("Version", TWILL_VERSION));
  fields.push_back(row("Build", TWILL_COMMIT));
  fields.push_back(row("Built", TWILL_BUILD_DATE));
  const AudioInfo ai = p_.audio_info();
  fields.push_back(row("Audio", std::to_string(ai.sample_rate) + " Hz, " + std::to_string(ai.block_frames) + " frames"));
  // Live: how long the audio takes to make, as a share of the time it has.
  fields.push_back({"Audio load", [this] { return load_text(); }, nullptr});
  fields.push_back({"Since start", [this] {
                      char b[48];
                      std::snprintf(b, sizeof b, "highest %.0f%%, %u late", engine_->load_highest() * 100,
                                    unsigned(engine_->late_blocks()));
                      return std::string(b);
                    }, nullptr});
  fields.push_back(row("Screen", std::to_string(size_.w) + " x " + std::to_string(size_.h)));
  // Free software: the GPL asks an interactive program to say so, and that there's no warranty.
  fields.push_back(row("License", "GPL-3.0, no warranty"));
  fields.push_back(row("Factory kit", "CC0, public domain"));
  open_inspector("About Twill Studio", std::move(fields));
  inspector_live_ = true;
}

std::string App::load_text() {
  float peak = 0;
  for (float p : load_peaks_) peak = std::max(peak, p);
  char b[40];
  std::snprintf(b, sizeof b, "%.0f%%, peaks %.0f%%", engine_->load_average() * 100, peak * 100);
  return b;
}

void App::open_controls() {
  // Every button combination, in one place: the hint bar shows only what fits, which on a 640x480
  // screen is about four. Headings are rows with no value.
  static const char* const kRows[][2] = {
      {"EVERYWHERE", ""},
      {"Start", "play / stop"},
      {"L1 or R1 + Start", "play from cursor"},
      {"L2 / R2", "prev / next view"},
      {"Sel + Left / Right", "prev / next view"},
      {"Sel + B / A", "undo / redo"},
      {"Sel + Y / R1", "copy / paste"},
      {"Sel + X, or Menu", "settings"},
      {"Menu + Start", "quit"},
      {"ARRANGEMENT", ""},
      {"A", "new clip / select"},
      {"A + Left / Right", "clip length"},
      {"B", "delete clip"},
      {"B + Left / Right", "move clip"},
      {"Y", "duplicate clip"},
      {"X", "open clip / panel"},
      {"L1 or R1 + D-pad", "zoom / page"},
      {"PIANO ROLL", ""},
      {"A", "add / select note"},
      {"A + Left / Right", "length"},
      {"A + Up / Down", "pitch"},
      {"B", "delete note"},
      {"B + D-pad", "move notes"},
      {"A or B + L1 + L / R", "fine"},
      {"A or B + R1 + U / D", "octave"},
      {"Y + D-pad", "select area"},
      {"Y + A", "select all"},
      {"X", "note / clip panel"},
      {"L1 or R1 + D-pad", "grid / page"},
      {"Sel + Up / Down", "notes / lane"},
      {"SOUND", ""},
      {"Up / Down", "choose"},
      {"Left / Right", "change"},
      {"A", "browse"},
      {"B", "reset"},
      {"Y", "listen"},
      {"X", "track settings"},
      {"Sel + Up / Down", "track"},
      {"MIXER", ""},
      {"A + D-pad", "change"},
      {"B", "reset"},
      {"Y", "listen"},
      {"X", "effects"},
      {"PANELS", ""},
      {"R1 + Left / Right", "bigger steps"},
      {"B or X", "close"},
  };
  std::vector<InspectorField> fields;
  for (const auto& r : kRows) {
    const std::string value = r[1];
    InspectorField field{r[0], [value] { return value; }, nullptr};
    field.heading = value.empty();
    field.buttons = !field.heading;
    fields.push_back(std::move(field));
  }
  open_inspector("Controls", std::move(fields));
}

void App::add_project_fields(std::vector<InspectorField>& fields) {
  fields.push_back({"Name", [this] { return project_.name; }, nullptr, [this] {
                      close_inspector();
                      name_edit_ = true;
                      name_track_ = -1;
                      name_draft_ = project_.name;
                      name_cursor_ = int(name_draft_.size());
                      invalidate_all();
                    }});
  fields.push_back({"Save", [] { return std::string("press A"); }, nullptr, [this] {
                      if (save_project_file()) message("Saved \"" + project_.name + "\"");
                    }});
  fields.push_back({"Load", [] { return std::string("press A"); }, nullptr, [this] {
                      std::vector<Platform::DirEntry> entries;
                      p_.list_dir(kProjectsFolder, entries);
                      std::vector<InspectorField> list;
                      for (const auto& e : entries) {
                        if (e.is_dir || e.name.size() < 7 || e.name.compare(e.name.size() - 6, 6, ".twill") != 0) continue;
                        const std::string name = e.name.substr(0, e.name.size() - 6);
                        list.push_back({name, [] { return std::string(""); }, nullptr, [this, name] {
                                          if (!confirm_discard("load")) return;
                                          if (load_project_file(name)) {
                                            close_inspector();
                                            message("Loaded \"" + name + "\"");
                                          }
                                        }});
                      }
                      if (list.empty()) {
                        message("No saved projects yet");
                        return;
                      }
                      open_inspector("Load a project", std::move(list));
                    }});
  fields.push_back({"New project", [] { return std::string("press A"); }, nullptr, [this] {
                      if (!confirm_discard("start a new project")) return;
                      new_project();
                      close_inspector();
                      message("New project");
                    }});
  fields.push_back({"MIDI clock",
                    [this] {
                      static const char* const names[] = {"Off", "Send", "Follow"};
                      return std::string(names[int(project_.midi_clock)]);
                    },
                    [this](int step, bool) {
                      // A setting, not an undoable edit.
                      project_.midi_clock = MidiClock((int(project_.midi_clock) + step + 3) % 3);
                      ++transport_changes_;  // the engine sends clock or not
                      saved_revision_ = ~0ull;
                      last_edit_us_ = p_.micros();
                    }});
  fields.push_back({"MIDI devices", [this] { return p_.midi() ? p_.midi()->describe() : std::string("none"); }, nullptr});
  // While on and playing, notes from a MIDI keyboard are recorded into the open clip ("Rec" on top).
  fields.push_back({"Record from MIDI", [this] { return std::string(recording_ ? "On" : "Off"); },
                    [this](int, bool) { toggle_recording(); }, [this] { toggle_recording(); }});
  fields.push_back({"Render to WAV", [] { return std::string("press A"); }, nullptr, [this] {
                      message("Rendering...");
                      if (render_song_to_wav()) message("Saved renders/" + file_safe(project_.name) + ".wav");
                    }});
  fields.push_back({"Render stems", [] { return std::string("press A"); }, nullptr, [this] {
                      message("Rendering stems...");
                      const int n = render_stems();
                      if (n > 0) message("Saved " + std::to_string(n) + " stems to renders/" + file_safe(project_.name) + " stems");
                      else message("No tracks to render");
                    }});
}

bool App::has_unsaved_changes() {
  std::vector<uint8_t> on_disk;
  const std::string path = std::string(kProjectsFolder) + "/" + file_safe(project_.name) + ".twill";
  if (!p_.read_file(path.c_str(), on_disk)) return true;
  return on_disk != save_project(project_);
}

bool App::confirm_discard(const char* what) {
  if (!has_unsaved_changes()) return true;
  const uint64_t now = p_.micros();
  if (confirm_what_ == what && now < confirm_until_us_) {
    confirm_what_.clear();
    return true;
  }
  confirm_what_ = what;
  confirm_until_us_ = now + 4000000;
  message(std::string("Unsaved changes: press A again to ") + what);
  return false;
}

bool App::save_project_file() {
  p_.make_dir(kProjectsFolder);
  const std::string path = std::string(kProjectsFolder) + "/" + file_safe(project_.name) + ".twill";
  if (!p_.write_file(path.c_str(), save_project(project_))) {
    message("Couldn't save " + path);
    return false;
  }
  return true;
}

void App::replace_project(Project p) {
  engine_->stop();
  project_ = std::move(p);
  history_.clear();
  open_.assign(project_.tracks.size(), Placement{});
  track_ = 0;
  validate_open_clips();
  sync_engine();
  invalidate_all();
}

bool App::load_project_file(const std::string& name) {
  std::vector<uint8_t> bytes;
  const std::string path = std::string(kProjectsFolder) + "/" + name + ".twill";
  Project p;
  std::string error;
  if (!p_.read_file(path.c_str(), bytes) || !load_project(bytes.data(), bytes.size(), p, error) || p.tracks.empty()) {
    message(error.empty() ? "Couldn't read " + path : "Couldn't load: " + error);
    return false;
  }
  replace_project(std::move(p));
  return true;
}

void App::new_project() {
  Project p;
  p.name = auto_names_ ? fresh_song_name() : "Untitled";
  p.loop = false;
  // Track 1 is the factory drum kit; the rest are macro oscillators set up for parts, and named for
  // them: virtual analog (bass), wavetable (chords), formant (lead), plucked string, string machine
  // (pad), two-op FM (keys), analog + filter.
  p.tracks.push_back(factory_kit_track());
  const int engines[7] = {8, 13, 11, 19, 6, 10, 0};
  const int voices[7] = {1, 3, 1, 3, 3, 3, 1};
  const char* names[7] = {"Bass", "Chords", "Lead", "Pluck", "Pad", "Keys", "Synth"};
  for (int i = 0; i < 7 && i + 1 < p_.max_tracks(); ++i) {
    Track t;
    t.name = names[i];
    t.colour = uint32_t(i + 1);
    t.macro.engine = engines[i];
    t.voices = voices[i];
    p.tracks.push_back(t);
  }
  replace_project(std::move(p));
}

std::vector<float> App::render_offline(const Project& song, bool master) {
  // Offline, on a separate engine: the whole song once, plus a few seconds for tails.
  const Tick end = std::max<Tick>(4 * kTicksPerQuarter, project_.song_end());
  Project p = song;
  p.loop = false;
  Engine e(std::max(16, p_.max_voices()));
  e.set_master_bypass(!master);
  e.submit(build_snapshot(p, p_.max_tracks(), 1, [this](const std::string& path) { return load_sample(path).get(); }));
  e.locate(0);
  e.play();
  const double seconds = end / double(kTicksPerQuarter) * 60.0 / p.tempo + 3.0;
  const int frames = int(seconds * Engine::kSampleRate);
  std::vector<float> out(size_t(frames) * 2);
  for (int pos = 0; pos < frames; pos += 256) e.render(&out[size_t(pos) * 2], std::min(256, frames - pos));
  return out;
}

int App::render_stems() {
  const int n = std::min(int(project_.tracks.size()), p_.max_tracks());
  bool any_solo = false;
  for (int i = 0; i < n; ++i) any_solo |= project_.tracks[size_t(i)].solo;
  const std::string folder = std::string(kRendersFolder) + "/" + file_safe(project_.name) + " stems";
  int written = 0;
  for (int k = 0; k < n; ++k) {
    const Track& t = project_.tracks[size_t(k)];
    // Only what the mix plays: not muted (or left out by a solo), with clips, and making sound.
    if (t.mute || (any_solo && !t.solo) || t.arrangement.empty() || t.instrument == InstrumentType::MidiOut) continue;
    Project p = project_;
    for (int i = 0; i < int(p.tracks.size()); ++i) {
      p.tracks[size_t(i)].solo = false;
      p.tracks[size_t(i)].mute = i != k;
    }
    const std::vector<float> out = render_offline(p, false);
    if (written == 0) p_.make_dir(folder.c_str());
    char num[8];
    std::snprintf(num, sizeof num, "%02d ", k + 1);
    const std::string path = folder + "/" + num + file_safe(t.name) + ".wav";
    if (!p_.write_file(path.c_str(), encode_wav_float(out.data(), int(out.size() / 2)))) {
      message("Couldn't write " + path);
      return written;
    }
    ++written;
  }
  return written;
}

bool App::render_song_to_wav() {
  const std::vector<float> out = render_offline(project_, true);
  const int frames = int(out.size() / 2);
  p_.make_dir(kRendersFolder);
  const std::string path = std::string(kRendersFolder) + "/" + file_safe(project_.name) + ".wav";
  if (!p_.write_file(path.c_str(), encode_wav16(out.data(), frames))) {
    message("Couldn't write " + path);
    return false;
  }
  return true;
}

void App::handle_midi() {
  MidiPort* port = p_.midi();
  if (!port) return;
  // Notes from the keyboard: play the current track, and record them if armed and playing.
  std::vector<MidiMessage> in;
  port->poll(in);
  if (!in.empty()) last_midi_in_us_ = p_.micros();
  const Placement* pl = open_placement();
  Clip* clip = open_clip();
  auto clip_time = [&]() -> Tick {
    const Tick pos = engine_->position();
    if (!pl || !clip || clip->length <= 0 || pos < pl->start || pos >= pl->start + pl->length) return -1;
    return (pos - pl->start + pl->offset) % clip->length;
  };
  const Tick q = kTicksPerQuarter / 4;  // recorded notes snap to 1/16
  const Track* cur = track_ < int(project_.tracks.size()) ? &project_.tracks[size_t(track_)] : nullptr;
  const bool to_midi_out = cur && cur->instrument == InstrumentType::MidiOut;
  for (MidiMessage m : in) {
    // Clock, start, continue, stop and song position: followed if the project says so.
    if (m.status >= 0xF8 || m.status == 0xF2) {
      if (project_.midi_clock != MidiClock::Follow) continue;
      if (m.time_us == 0) m.time_us = midi_now_us();
      const ClockFollower::Action a = clock_in_.feed(m);
      if (a.kind == ClockFollower::Action::Play) {
        if (a.from_start) play_from(view_ == View::PianoRoll && pl ? pl->start : 0);
        else engine_->play();
      } else if (a.kind == ClockFollower::Action::Stop) {
        engine_->stop();
      } else if (a.kind == ClockFollower::Action::Locate) {
        engine_->locate(a.position);
      }
      if (a.kind != ClockFollower::Action::None) invalidate(top_bar_);
      continue;
    }
    if (m.is_note_on()) {
      if (to_midi_out) {
        // A MIDI Out track: the note goes on to its channel, but not back to the device it came
        // from, which plays its own keys (and might echo, which would loop).
        const uint8_t ch = uint8_t(cur->midi_channel & 15);
        port->send_except(MidiMessage{uint8_t(0x90 | ch), m.data1, m.data2}, m.source);
        live_out_[m.data1 & 127] = LiveOut{int8_t(ch), m.source};
      } else {
        engine_->live_note(track_, m.data1, m.data2);
      }
      const Tick ct = clip_time();
      if (recording_ && engine_->playing() && ct >= 0) {
        Note n;
        n.start = std::min(clip->length - q, (ct + q / 2) / q * q);
        n.length = q;
        n.pitch = m.data1;
        n.velocity = m.data2;
        if (uint32_t id = history_.add_note(clip->id, n)) held_notes_.push_back(HeldNote{m.data1, id, n.start});
      }
    } else if (m.is_note_off()) {
      LiveOut& out = live_out_[m.data1 & 127];
      if (out.channel >= 0) {  // ends where it started, even if the track has changed since
        port->send_except(MidiMessage{uint8_t(0x80 | out.channel), m.data1, 0}, out.source);
        out.channel = -1;
      } else {
        engine_->live_note(track_, m.data1, 0);
      }
      for (size_t i = 0; i < held_notes_.size(); ++i) {
        if (held_notes_[i].pitch != m.data1) continue;
        const HeldNote h = held_notes_[i];
        held_notes_.erase(held_notes_.begin() + long(i));
        clip = open_clip();
        const Note* n = clip ? clip->find(h.note_id) : nullptr;
        const Tick ct = clip_time();
        if (!n || ct < 0) break;
        Note copy = *n;
        const Tick len = ct >= h.start ? ct - h.start : clip->length - h.start;  // past the loop point: to the end
        copy.length = std::max(q, std::min(clip->length - h.start, (len + q / 2) / q * q));
        history_.change_note(clip->id, copy);
        break;
      }
    }
  }
  follow_clock();
  // MIDI out (notes on MIDI Out tracks, and clock) comes from the engine, stamped to the sample.
  // With simulated time (tests) it goes straight out here; otherwise MidiSender's thread sends it.
  if (!p_.real_time() && midi_sender_) midi_sender_->pump(INT64_MAX);
}

int64_t App::midi_now_us() const {
  return p_.real_time() ? MidiSender::now_us() : int64_t(p_.micros());
}

void App::follow_clock() {
  if (project_.midi_clock != MidiClock::Follow) {
    if (tempo_override_ != 0) engine_->set_tempo_override(0);
    tempo_override_ = 0;
    clock_in_.reset();
    return;
  }
  if (!clock_in_.has_tempo()) return;
  double bpm = clock_in_.bpm();
  if (clock_in_.running() && engine_->playing()) {
    // Where the engine is, as heard (its count is from the latest callback, on average half a
    // block old, and reaches the speaker after the output latency), against the master.
    const double beats_per_frame = bpm / 60.0 / Engine::kSampleRate;
    const double engine_beats = engine_->elapsed_ticks() / kTicksPerQuarter +
                                (0.5 * block_frames_ - output_latency_) * beats_per_frame;
    const double error = engine_beats - clock_in_.beats_at(midi_now_us());
    phase_error_ += (error - phase_error_) * 0.2;
    // Ahead: a little slower; behind: a little faster (at most 3%), closing the gap in about a second.
    bpm *= 1 - std::max(-0.03, std::min(0.03, phase_error_ * 0.5));
  } else {
    phase_error_ = 0;
  }
  if (std::fabs(bpm - tempo_override_) > 1e-4 * bpm) {
    tempo_override_ = bpm;
    engine_->set_tempo_override(bpm);
    invalidate(top_bar_);
  }
}

void App::handle_name_edit() {
  const InputTracker& in = input_;
  const int n = int(sizeof kNameChars) - 1;
  if (name_cursor_ >= int(name_draft_.size())) name_draft_.resize(size_t(name_cursor_) + 1, ' ');
  char& ch = name_draft_[size_t(name_cursor_)];
  int idx = 0;
  while (idx < n && kNameChars[idx] != ch) ++idx;
  const int step = in.pressed_or_repeat(kUp) ? 1 : in.pressed_or_repeat(kDown) ? -1 : 0;
  if (step) ch = kNameChars[(idx + step * (in.held(kR) ? 10 : 1) + 10 * n) % n];
  if (in.pressed_or_repeat(kRight) && name_cursor_ < 31) ++name_cursor_;
  if (in.pressed_or_repeat(kLeft) && name_cursor_ > 0) --name_cursor_;
  if (in.pressed(kY)) name_draft_.erase(size_t(name_cursor_), 1);  // delete the character
  const bool track = name_track_ >= 0 && name_track_ < int(project_.tracks.size());
  if (in.pressed(kX)) {  // a song: a made-up name; a track: the next part name
    std::string draft = name_draft_;
    while (!draft.empty() && draft.back() == ' ') draft.pop_back();
    name_draft_ = track ? next_role_name(draft, 1) : fresh_song_name();
    name_cursor_ = int(name_draft_.size());
  }
  if (in.pressed(kA) || in.pressed(kB)) {
    if (in.pressed(kA)) {
      std::string name = name_draft_;
      while (!name.empty() && name.back() == ' ') name.pop_back();
      while (!name.empty() && name.front() == ' ') name.erase(name.begin());
      if (!name.empty() && track) {
        Track t = project_.tracks[size_t(name_track_)];
        if (t.name != name) {
          t.name = name;
          history_.change_track(name_track_, t);
        }
      } else if (!name.empty()) {
        project_.name = name;  // a song's name isn't an undoable edit
        ++transport_changes_;
        last_edit_us_ = p_.micros();
        saved_revision_ = ~0ull;  // autosave the new name
      }
    }
    name_edit_ = false;
    if (track) arrangement_->open_track_settings(name_track_, 0);  // back to the panel, on the Name row
  }
  invalidate_all();
}

const std::vector<std::string>& App::role_names() {
  static const std::vector<std::string> names = {
      "Drums", "Kick",  "Snare", "Hats",    "Perc",  "Bass",  "Sub",   "Chords", "Keys", "Piano",
      "Organ", "Pad",   "Strings", "Lead",  "Pluck", "Arp",   "Rep",   "Bells",  "Brass", "Vox",
      "Choir",
      "Synth", "Drone", "Noise", "FX",
  };
  return names;
}

std::string App::next_role_name(const std::string& name, int step) {
  const auto& names = role_names();
  const int n = int(names.size());
  int at = -1;
  for (int i = 0; i < n; ++i)
    if (caps(names[size_t(i)]) == caps(name)) at = i;
  // A name not in the list starts the list from its beginning (or, going back, its end).
  const int next = at < 0 ? (step > 0 ? 0 : n - 1) : (at + step + n) % n;
  return names[size_t(next)];
}

void App::step_track_name(int track, int step) {
  if (track < 0 || track >= int(project_.tracks.size())) return;
  Track t = project_.tracks[size_t(track)];
  t.name = next_role_name(t.name, step);
  history_.change_track(track, t);
}

void App::edit_track_name(int track) {
  if (track < 0 || track >= int(project_.tracks.size())) return;
  close_inspector();
  name_edit_ = true;
  name_track_ = track;
  name_draft_ = project_.tracks[size_t(track)].name;
  name_cursor_ = int(name_draft_.size());
  invalidate_all();
}

void App::draw_name_edit(Canvas& c) {
  const Rect r = inspector_rect_;
  const bool track = name_track_ >= 0;
  draw_panel_frame(c, track ? "TRACK NAME" : "PROJECT NAME");
  const Font& f = Font::get(FontFace::Regular, u(kPanelText));
  const Font& big = Font::get(FontFace::SemiBold, u(20));
  const Font& faint = Font::get(FontFace::Regular, u(14));
  // The name as ordinary text. The letter being changed is in the cursor's colour on a tint,
  // with the letters Up and Down would give shown faintly above and below it, like a wheel.
  const int y = r.y + int(u(84));
  const int n = int(sizeof kNameChars) - 1;
  auto glyph = [&](int i) { return std::string(1, i < int(name_draft_.size()) ? name_draft_[size_t(i)] : ' '); };
  // A space (or the end of the name) still needs room to show where the cursor is.
  const float slot_min = big.measure("n");
  auto width = [&](int i) {
    const float w = big.measure(glyph(i).c_str());
    return i == name_cursor_ ? std::max(w, slot_min) : w;
  };
  const int count = std::max(int(name_draft_.size()), name_cursor_ + 1);
  float cursor_x = 0;
  for (int i = 0; i < name_cursor_; ++i) cursor_x += width(i);
  const float avail = float(r.w) - u(24);
  const float shift = std::max(0.0f, cursor_x + width(name_cursor_) - avail);  // long names scroll
  const Rect saved = c.clip();
  c.set_clip(saved.intersect(Rect{r.x + int(u(12)), r.y, int(avail), r.h}));
  float x = r.x + u(12) - shift;
  for (int i = 0; i < count; ++i) {
    const std::string g = glyph(i);
    const float w = width(i);
    if (i == name_cursor_) {
      // Tinted like a text selection, so even a space shows as a slot.
      c.fill(Rect{int(x), y - int(big.ascent()) - int(u(2)), std::max(2, int(w + 0.5f)), int(big.ascent()) + int(u(7))},
             theme_.selection_fill);
      const float gx = x + (w - big.measure(g.c_str())) / 2;
      c.text(big, gx, y, g.c_str(), theme_.cursor);
      // The wheel: what Up (above) and Down (below) would make it.
      int idx = 0;
      while (idx < n && kNameChars[idx] != g[0]) ++idx;
      for (int dir : {1, -1}) {
        const char ch = kNameChars[(idx + dir + n) % n];
        const char one[2] = {ch, 0};
        const char* str = ch == ' ' ? "space" : one;
        const float fx = x + (w - faint.measure(str)) / 2;
        c.text(faint, fx, dir > 0 ? y - int(u(26)) : y + int(u(26)), str, theme_.text_secondary);
      }
    } else {
      c.text(big, x, y, g.c_str(), theme_.text);
    }
    x += w;
  }
  c.set_clip(saved);
  int ty = y + int(u(56));
  for (const char* line : {"Up/Down: change letter", "Left/Right: move", "Y: delete letter",
                           track ? "X: next part name" : "X: make up a name", "A: done   B: cancel"}) {
    c.text(f, r.x + u(12), ty, line, theme_.text_secondary);
    ty += int(u(20));
  }
}

// --- Settings --------------------------------------------------------------------------------

namespace {
constexpr const char* kSettingsName = "settings.txt";  // "key=value" lines
}  // namespace

void App::load_settings() {
  std::vector<uint8_t> bytes;
  if (!p_.read_file(kSettingsName, bytes)) return;
  const std::string text(bytes.begin(), bytes.end());
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(at, end - at);
    if (line == "theme=dark") theme_ = Theme::dark_theme();
    if (line == "theme=light") theme_ = Theme::light();
    if (line == "names=auto") auto_names_ = true;
    if (line == "names=untitled") auto_names_ = false;
    at = end + 1;
  }
}

void App::save_settings() {
  const std::string text = std::string("theme=") + (theme_.dark ? "dark" : "light") + "\n" +
                           "names=" + (auto_names_ ? "auto" : "untitled") + "\n";
  p_.write_file(kSettingsName, std::vector<uint8_t>(text.begin(), text.end()));
}

void App::set_auto_names(bool on) {
  auto_names_ = on;
  save_settings();
}

std::string App::fresh_song_name() {
  std::vector<Platform::DirEntry> saved;
  p_.list_dir(kProjectsFolder, saved);
  std::string name;
  for (int tries = 0; tries < 50; ++tries) {
    name = song_name(uint32_t(p_.micros()) ^ (++names_made_ * 0x632BE5ABu));
    bool taken = name == project_.name;
    for (const auto& e : saved) taken = taken || e.name == file_safe(name) + ".twill";
    if (!taken) break;
  }
  return name;
}

void App::set_dark_mode(bool on) {
  if (on == theme_.dark) return;
  theme_ = on ? Theme::dark_theme() : Theme::light();
  save_settings();
  invalidate_all();
}

// --- Sounds ---------------------------------------------------------------------------------

int App::sound_index(const Track& t) {
  switch (t.instrument) {
    case InstrumentType::Macro: return t.macro.engine;
    case InstrumentType::Sampler: return PlaitsVoice::kNumEngines;
    case InstrumentType::MidiOut: return PlaitsVoice::kNumEngines + 1;
  }
  return 0;
}

const char* App::sound_name(const Track& t) {
  switch (t.instrument) {
    case InstrumentType::Macro: return PlaitsVoice::engine_name(t.macro.engine);
    case InstrumentType::Sampler: return "Sampler";
    case InstrumentType::MidiOut: return "MIDI out";
  }
  return "?";
}

void App::step_sound(int track, int step) {
  if (track < 0 || track >= int(project_.tracks.size())) return;
  Track t = project_.tracks[size_t(track)];
  const int i = (sound_index(t) + step + kNumSounds) % kNumSounds;
  if (i < PlaitsVoice::kNumEngines) {
    t.instrument = InstrumentType::Macro;
    t.macro.engine = i;
  } else {
    t.instrument = i == PlaitsVoice::kNumEngines ? InstrumentType::Sampler : InstrumentType::MidiOut;
  }
  history_.change_track(track, t);
  if (t.instrument != InstrumentType::MidiOut) audition(track, 60);
}

// --- Inspector ------------------------------------------------------------------------------

void App::open_inspector(std::string title, std::vector<InspectorField> fields) {
  inspector_title_ = std::move(title);
  inspector_fields_ = std::move(fields);
  inspector_live_ = false;
  inspector_sel_ = 0;
  inspector_scroll_ = 0;
  while (inspector_sel_ + 1 < int(inspector_fields_.size()) && inspector_fields_[size_t(inspector_sel_)].heading)
    ++inspector_sel_;
  invalidate_all();
}

void App::select_inspector_row(int row) {
  if (inspector_fields_.empty()) return;
  inspector_sel_ = std::max(0, std::min(row, int(inspector_fields_.size()) - 1));
  invalidate_all();  // the panel's own drawing scrolls the row into view
}

void App::close_inspector() {
  menu_panel_ = false;
  if (inspector_fields_.empty()) return;
  inspector_fields_.clear();
  invalidate_all();
}

void App::handle_inspector() {
  const InputTracker& in = input_;
  if (in.pressed(kX) || in.pressed(kB)) {
    close_inspector();
    return;
  }
  const int n = int(inspector_fields_.size());
  const int dir = in.pressed_or_repeat(kUp) ? -1 : in.pressed_or_repeat(kDown) ? 1 : 0;
  if (dir) {
    // Headings are passed over (a panel of nothing but headings would stop after one lap).
    for (int i = 0; i < n; ++i) {
      inspector_sel_ = (inspector_sel_ + dir + n) % n;
      if (!inspector_fields_[size_t(inspector_sel_)].heading) break;
    }
  }
  InspectorField& field = inspector_fields_[size_t(inspector_sel_)];
  int step = in.pressed_or_repeat(kRight) ? 1 : in.pressed_or_repeat(kLeft) ? -1 : 0;
  if (step && field.adjust) {
    auto adjust = field.adjust;  // may replace the panel
    adjust(step, in.held(kR));
  }
  if (in.pressed(kA) && field.press) {
    auto press = field.press;  // may close or replace the panel
    press();
    invalidate_all();
    return;
  }
  if (in.pressed_or_repeat(kUp | kDown | kLeft | kRight)) invalidate_all();
}

// --- Frame ----------------------------------------------------------------------------------

void App::handle_global() {
  const InputTracker& in = input_;
  if (in.held(kMenu) && in.pressed(kStart)) {
    quit_ = true;
    return;
  }
  // Menu on its own: the project panel (tempo, key, scale, save and load...), from any view.
  if (in.pressed(kMenu)) menu_used_ = false;
  if (in.held(kMenu) && in.pressed(~uint32_t(kMenu))) menu_used_ = true;
  if (in.released(kMenu) && !menu_used_ && !name_edit_) {
    toggle_settings();  // a second tap puts it away
    return;
  }
  // L2 / R2: the previous / next view, in one press (Select + Left / Right does the same).
  if (!name_edit_ && (in.pressed(kL2) || in.pressed(kR2))) {
    step_view(in.pressed(kR2) ? 1 : -1);
    return;
  }
  if (in.held(kSelect)) {
    if (in.pressed(kB)) message(history_.undo() ? "Undone" : "Nothing to undo");
    if (in.pressed(kA)) message(history_.redo() ? "Redone" : "Nothing to redo");
    if (in.pressed(kLeft) || in.pressed(kRight)) step_view(in.pressed(kRight) ? 1 : -1);
    if (view_ == View::PianoRoll && (in.pressed(kUp) || in.pressed(kDown))) roll_->set_lane_focus(in.pressed(kDown));
    // In the sound view, Up / Down choose the track (elsewhere the arrangement is where to pick one).
    if (view_ == View::Instrument && (in.pressed(kUp) || in.pressed(kDown))) set_track(track_ + (in.pressed(kDown) ? 1 : -1));
    // Select + X: the settings panel, as Menu opens it. Menu is awkward to reach on some handhelds
    // (or kept by the system), and not all of them have sticks to click.
    if (in.pressed(kX) && !name_edit_) toggle_settings();
    // Copy and paste: Y copies, R pastes at the cursor.
    if (in.pressed(kY) || in.pressed(kR)) {
      if (view_ == View::PianoRoll) in.pressed(kY) ? roll_->copy() : roll_->paste();
      else if (view_ == View::Arrangement) in.pressed(kY) ? arrangement_->copy() : arrangement_->paste();
    }
    return;
  }
  // L or R + Start: play from the cursor. (Select + Start is the handhelds' usual "close the app"
  // shortcut, so it's left alone.)
  if (in.pressed(kStart) && (in.held(kL) || in.held(kR))) {
    play_from(view_ == View::PianoRoll ? roll_->cursor_song_position() : arrangement_->cursor_song_position());
    invalidate(top_bar_);
    return;
  }
  if (in.pressed(kStart)) {
    if (engine_->playing()) {
      engine_->stop();
    } else {
      // From the arrangement: the whole song. From the roll: the clip being edited.
      const Placement* pl = open_placement();
      play_from(view_ == View::PianoRoll && pl ? pl->start : project_.loop_start);
    }
    invalidate(top_bar_);
  }
}

void App::play_from(Tick position) {
  // The loop follows the view: the edited clip's placement in the roll, the song in the
  // arrangement. Transport settings aren't edits, so they bypass the undo history.
  const Placement* pl = open_placement();
  if (view_ == View::PianoRoll && pl) {
    project_.loop_start = pl->start;
    project_.loop_end = pl->start + pl->length;
  } else {
    const Tick bar = 4 * kTicksPerQuarter;
    project_.loop_start = 0;
    project_.loop_end = std::max(bar, (project_.song_end() + bar - 1) / bar * bar);
  }
  project_.loop = true;
  if (position < project_.loop_start || position >= project_.loop_end) position = project_.loop_start;
  ++transport_changes_;
  sync_engine();
  engine_->locate(position);
  engine_->play();
  invalidate(top_bar_);
}

void App::sync_engine() {
  if (history_.revision() == synced_revision_ && transport_changes_ == synced_transport_) return;
  synced_revision_ = history_.revision();
  synced_transport_ = transport_changes_;
  engine_->submit(build_snapshot(project_, p_.max_tracks(), ++snapshot_counter_,
                                 [this](const std::string& path) { return load_sample(path).get(); }));
}

void App::autosave(bool force) {
  if (history_.revision() == saved_revision_) return;  // (~0 forces a save after a rename)
  if (!force && p_.micros() - last_edit_us_ < kAutosaveDelayUs) return;
  if (p_.write_file(kAutosaveName, save_project(project_))) saved_revision_ = history_.revision();
  else p_.log("autosave failed");
}

bool App::frame(const InputState& state, Canvas& canvas) {
  const uint64_t now = p_.micros();
  input_.update(state.held, now);
  if (state.quit_requested) quit_ = true;
  if (canvas.width() != size_.w || canvas.height() != size_.h) layout(canvas);
  // Twice a second: the audio load's peak into the last 3 s, and a live panel redrawn to show it.
  if (now - live_redraw_us_ >= 500000) {
    live_redraw_us_ = now;
    load_slot_ = (load_slot_ + 1) % 6;
    load_peaks_[load_slot_] = engine_->take_load_peak();
    if (inspector_open() && inspector_live_) invalidate(inspector_rect_);
  }

  handle_global();
  // While Select or Menu is held, buttons belong to the global chords above, not the view.
  if (!input_.held(kSelect) && !input_.held(kMenu)) {
    if (name_edit_) handle_name_edit();
    else if (inspector_open()) handle_inspector();
    else if (view_ == View::Arrangement) arrangement_->handle_input();
    else if (view_ == View::PianoRoll) roll_->handle_input();
    else if (view_ == View::Instrument) instrument_->handle_input();
    else mixer_->handle_input();
  }

  if (history_.revision() != last_revision_seen_) {
    last_revision_seen_ = history_.revision();
    last_edit_us_ = now;
    validate_open_clips();
    invalidate_all();
  }
  handle_midi();
  // Playing music, or played on a MIDI keyboard in the last five minutes: no dimming or sleeping.
  const bool awake = engine_->playing() || (last_midi_in_us_ && p_.micros() - last_midi_in_us_ < 300000000ull);
  if (awake != awake_) {
    awake_ = awake;
    p_.keep_awake(awake);
  }
  sync_engine();
  engine_->collect_garbage();
  free_unused_samples();
  autosave(false);

  if (view_ == View::Arrangement) arrangement_->tick();
  else if (view_ == View::PianoRoll) roll_->tick();
  else if (view_ == View::Instrument) instrument_->tick();
  else mixer_->tick();
  if (!message_.empty() && now > message_until_us_) {
    message_.clear();
    invalidate(hint_bar_);
  }
  if (input_.pressed(~0u) || input_.released(~0u)) invalidate(hint_bar_);  // hints follow held buttons
  if (engine_->playing()) invalidate(top_bar_);  // the position readout
  // Out of voices: say so for a moment, rather than notes just dropping out.
  const uint32_t stolen = engine_->voices_stolen();
  if (stolen != voices_stolen_seen_) {
    voices_stolen_seen_ = stolen;
    voices_full_until_us_ = now + 1500000;
    invalidate(top_bar_);
  } else if (voices_full_until_us_ && now > voices_full_until_us_) {
    voices_full_until_us_ = 0;
    invalidate(top_bar_);
  }

  draw(canvas);
  return !quit_;
}

// --- Drawing --------------------------------------------------------------------------------

void App::invalidate(const Rect& r) {
  if (!r.empty()) dirty_.push_back(r);
}

void App::invalidate_all() { invalidate(Rect{0, 0, size_.w, size_.h}); }

void App::layout(const Canvas& c) {
  size_ = Size{c.width(), c.height()};
  int top = int(u(kTopBarH)), hints = int(u(kHintBarH));
  top_bar_ = Rect{0, 0, size_.w, top};
  hint_bar_ = Rect{0, size_.h - hints, size_.w, hints};
  content_ = Rect{0, top, size_.w, size_.h - top - hints};
  int iw = int(u(kPanelW));
  inspector_rect_ = Rect{content_.right() - iw, content_.y, iw, content_.h};
  invalidate_all();
}

void App::draw(Canvas& c) {
  if (dirty_.empty()) return;
  // Merge into a few rectangles, then draw each in horizontal strips: a strip stays in the CPU
  // cache while every layer is drawn over it (design §7, measured in M0).
  Rect all;
  for (const Rect& r : dirty_) all = all.unite(r);
  std::vector<Rect> regions = dirty_.size() > 8 ? std::vector<Rect>{all} : dirty_;
  dirty_.clear();
  for (const Rect& region : regions) {
    for (int y = region.y - region.y % kStrip; y < region.bottom(); y += kStrip) {
      Rect strip = Rect{0, y, size_.w, kStrip}.intersect(region);
      if (strip.empty()) continue;
      c.set_clip(strip);
      if (!strip.intersect(top_bar_).empty()) draw_top_bar(c);
      if (!strip.intersect(content_).empty()) {
        if (view_ == View::Arrangement) arrangement_->draw(c);
        else if (view_ == View::PianoRoll) roll_->draw(c);
        else if (view_ == View::Instrument) instrument_->draw(c);
        else mixer_->draw(c);
        if (inspector_open()) draw_inspector(c);
        if (name_edit_) draw_name_edit(c);
      }
      if (!strip.intersect(hint_bar_).empty()) draw_hint_bar(c);
    }
  }
  c.reset_clip();
}

void App::draw_top_bar(Canvas& c) {
  const Rect& r = top_bar_;
  c.fill(r, theme_.header);
  c.fill(Rect{r.x, r.bottom() - std::max(1, int(u(1))), r.w, std::max(1, int(u(1)))}, theme_.text);
  const Font& fb = Font::get(FontFace::CondensedBold, u(kTopText));
  const Font& fs = Font::get(FontFace::CondensedSemiBold, u(kTopText - 2));
  const int base = fb.baseline_in(r.y, r.h - int(u(1)));
  const int mid = base - fb.cap_height() / 2;

  // Right side first, so the left side knows how much room it has: the transport. A triangle when
  // playing, a square when stopped; while notes are cut off for want of voices, "VOICES FULL".
  char pos_text[16], tempo_text[32];
  const Tick pos = engine_->position();
  const Tick bar = 4 * kTicksPerQuarter;
  std::snprintf(pos_text, sizeof pos_text, "%d.%d", pos / bar + 1, pos % bar / kTicksPerQuarter + 1);
  if (tempo_override_ > 0)  // following a MIDI clock: the master's tempo
    std::snprintf(tempo_text, sizeof tempo_text, "EXT %.1f BPM", clock_in_.bpm());
  else
    std::snprintf(tempo_text, sizeof tempo_text, "%.0f BPM", project_.tempo);
  const bool full = voices_full_until_us_ != 0;
  const float gap = u(12), icon = u(10);
  float x_right = r.right() - u(10) - fb.measure(tempo_text);
  c.text(fb, x_right, base, tempo_text, theme_.text);
  x_right -= gap + fb.measure(pos_text);
  c.text(fb, x_right, base, pos_text, theme_.text);
  if (full) {
    x_right -= u(8) + fb.measure("VOICES FULL");
    c.text(fb, x_right, base, "VOICES FULL", theme_.warning);
  } else {
    x_right -= u(7) + icon;
    const int ix = int(x_right), half = int(icon / 2);
    if (engine_->playing()) {  // a triangle pointing right
      for (int i = 0; i <= half; ++i) c.fill(Rect{ix, mid - half + i, int(icon * i / half * 0.8f) + 1, 1}, theme_.text);
      for (int i = 0; i < half; ++i) c.fill(Rect{ix, mid + half - i, int(icon * i / half * 0.8f) + 1, 1}, theme_.text);
    } else {
      c.fill(Rect{ix + int(u(1)), mid - half + int(u(1)), int(icon - u(2)), int(icon - u(2))}, theme_.text_secondary);
    }
  }
  if (recording_) {
    x_right -= gap + fb.measure("REC");
    c.text(fb, x_right, base, "REC", theme_.cursor);
    const int d = int(u(8));
    x_right -= u(4) + d;
    c.fill_rounded(Rect{int(x_right), mid - d / 2, d, d}, d / 2.0f, theme_.cursor);
  }

  // View tabs: the current one an ink block.
  float x = r.x + u(6);
  for (View v : {View::Arrangement, View::PianoRoll, View::Instrument, View::Mixer}) {
    const char* name = v == View::Arrangement ? "ARRANGE" : v == View::PianoRoll ? "ROLL" : v == View::Instrument ? "SOUND" : "MIX";
    const float w = fb.measure(name) + u(14);
    if (v == view_) {
      c.fill(Rect{int(x), r.y + int(u(5)), int(w), r.h - int(u(10))}, theme_.chip_active);
      c.text(fb, x + u(7), base, name, theme_.chip_active_text);
    } else {
      c.text(fb, x + u(7), base, name, theme_.text_secondary);
    }
    x += w + u(2);
  }

  // Current track: colour swatch, name, and engine if there's room.
  x += u(12);
  if (!project_.tracks.empty()) {
    const Track& t = project_.tracks[size_t(track_)];
    const int d = int(u(10));
    // The name, shortened if it would run into the transport; with no room for a few letters
    // (a square screen), no swatch or name at all.
    std::string name = caps(t.name);
    const float room = x_right - gap - (x + d + u(6));
    if (room < fb.measure("KIC")) return;
    c.fill(Rect{int(x), mid - d / 2, d, d}, track_colour(track_));
    while (name.size() > 1 && fb.measure(name.c_str()) > room) name.pop_back();
    x = c.text(fb, x + d + u(6), base, name.c_str(), theme_.text);
    const std::string engine = caps(sound_name(t));
    const float sp = u(kCapsSpacing);
    if (x + u(8) + fs.measure(engine.c_str(), sp) < x_right - gap)
      c.text(fs, x + u(8), base, engine.c_str(), theme_.text_secondary, sp);
  }
}

void App::draw_hint_bar(Canvas& c) {
  const Rect& r = hint_bar_;
  c.fill(r, theme_.hint_bar);
  const Font& fb = Font::get(FontFace::CondensedBold, u(kHintText));
  const Font& fs = Font::get(FontFace::CondensedSemiBold, u(kHintText));
  const int base = fs.baseline_in(r.y, r.h);
  const float sp = u(0.4f);

  std::vector<Hint> hints;
  if (input_.held(kSelect)) {
    // Most important first: the bar drops what doesn't fit (a 640-pixel screen shows about four).
    hints = {{"Left/Right", "view"}, {"X", "settings"}, {"B", "undo"}, {"A", "redo"}};
    if (view_ == View::Instrument) hints.push_back({"Up/Down", "track"});
    if (view_ == View::PianoRoll || view_ == View::Arrangement) {
      hints.push_back({"Y", "copy"});
      hints.push_back({"R", "paste"});
    }
    if (view_ == View::PianoRoll) hints.push_back({"Up/Down", "notes/lane"});
  } else if (input_.held(kMenu)) {
    hints = {{"release", "settings: theme, tempo, save..."}, {"Start", "quit"}};
  } else if (inspector_open()) {
    // Only what this panel's rows can do: a read-only page (Controls, About) just scrolls.
    bool adjusts = false, presses = false;
    for (const InspectorField& field : inspector_fields_) adjusts |= bool(field.adjust), presses |= bool(field.press);
    hints = {{"Up/Down", adjusts || presses ? "choose" : "scroll"}};
    if (adjusts) hints.push_back({"Left/Right", "change"});
    if (presses) hints.push_back({"A", "do"});
    if (adjusts) hints.push_back({"R+", "bigger steps"});
    hints.push_back({"X/B", "close"});
  } else {
    hints = view_ == View::Arrangement ? arrangement_->hints()
            : view_ == View::PianoRoll ? roll_->hints()
            : view_ == View::Instrument ? instrument_->hints()
                                        : mixer_->hints();
    // With no button held, say where the settings are, second so that small screens show it too:
    // the panel is where to find everything else.
    if (!input_.any_held()) hints.insert(hints.begin() + std::min<size_t>(1, hints.size()), {"Sel+X", "settings"});
  }
  // A message takes the right end; hints fill from the left, most important first, and stop
  // before they would run into it or off the screen. Each hint is its buttons as a keycap, then
  // what they do.
  float limit = r.right() - u(8);
  if (!message_.empty()) {
    const std::string m = caps(message_);
    const float w = fs.measure(m.c_str(), sp);
    c.text(fs, limit - w, base, m.c_str(), theme_.cursor.mix(theme_.hint_text, 60), sp);
    limit -= w + u(16);
  }
  float x = r.x + u(7);
  const int cap_y = r.y + int(u(5)), cap_h = r.h - int(u(10));
  for (const Hint& h : hints) {
    const std::string keys = caps(h.keys), action = caps(h.action);
    const int kw = keycap_width(fb, keys.c_str(), u(1));
    const float w = kw + u(5) + fs.measure(action.c_str(), sp);
    if (x + w > limit) break;
    draw_keycap(c, fb, int(x), cap_y, cap_h, keys.c_str(), theme_.keycap, theme_.keycap_text, false, u(1));
    x = c.text(fs, x + kw + u(5), base, action.c_str(), theme_.hint_text, sp) + u(11);
  }
}

int App::draw_panel_frame(Canvas& c, const char* title) {
  // A sheet a shade lighter than the page, casting a soft shadow to the left.
  const Rect& r = inspector_rect_;
  draw_shadow(c, r.x, r.y, r.h, int(u(8)), true, theme_.shadow_strength);
  c.fill(r, theme_.panel);
  c.vline(r.x, r.y, r.bottom(), theme_.grid_bar);
  const Font& tf = Font::get(FontFace::CondensedBold, u(kPanelTitle));
  c.text(tf, r.x + u(12), r.y + int(u(24)), title, theme_.text);
  const int rule_y = r.y + int(u(32));
  c.fill(Rect{r.x + int(u(8)), rule_y, r.w - int(u(16)), std::max(1, int(u(1)))}, theme_.text);
  return rule_y + int(u(4));
}

void App::draw_inspector(Canvas& c) {
  const Rect& r = inspector_rect_;
  const std::string title_caps = caps(inspector_title_);
  int y = draw_panel_frame(c, title_caps.c_str());
  const Font& label_f = Font::get(FontFace::CondensedSemiBold, u(kPanelText));
  const Font& value_f = Font::get(FontFace::CondensedBold, u(kPanelText + 2));
  const Font& action_f = Font::get(FontFace::CondensedSemiBold, u(kPanelText + 1));
  const Font& key_f = Font::get(FontFace::CondensedBold, u(kPanelText - 1));
  const float sp = u(kCapsSpacing);
  const int row_h = int(u(kPanelRowH));

  // Rows that don't fit scroll, keeping the selection in view. The last row shown may be cut off
  // at the bottom, which hints at more; arrows beside the title say which way there's more.
  const int n = int(inspector_fields_.size());
  const Rect list{r.x, y, r.w, r.bottom() - int(u(4)) - y};
  const int visible = std::max(1, list.h / row_h);
  // A heading just above the selection comes into view with it.
  int top = inspector_sel_;
  if (top > 0 && inspector_fields_[size_t(top - 1)].heading) --top;
  if (top < inspector_scroll_) inspector_scroll_ = top;
  if (inspector_sel_ >= inspector_scroll_ + visible) inspector_scroll_ = inspector_sel_ - visible + 1;
  inspector_scroll_ = std::max(0, std::min(inspector_scroll_, n - visible));
  const bool more_above = inspector_scroll_ > 0, more_below = inspector_scroll_ + visible < n;
  const int ah = int(u(5));  // arrow height
  auto arrow = [&](int cx, int tip_y, bool up) {
    for (int i = 0; i < ah; ++i) {
      const int row = up ? tip_y + i : tip_y - i;
      c.fill(Rect{cx - i, row, 2 * i + 1, 1}, theme_.text_secondary);
    }
  };
  const int ax = r.right() - int(u(18)), amid = r.y + int(u(17));
  if (more_above) arrow(ax, amid - ah - int(u(1)), true);
  if (more_below) arrow(ax, amid + ah + int(u(1)), false);

  const Rect saved = c.clip();
  c.set_clip(saved.intersect(list));
  for (int i = inspector_scroll_; i < n && y < list.bottom(); ++i) {
    const InspectorField& field = inspector_fields_[size_t(i)];
    const bool sel = i == inspector_sel_;
    if (field.heading) {
      // A section title: bold spaced capitals with a rule running on to the right.
      const int base = value_f.baseline_in(y, row_h) + int(u(2));
      const float end = c.text(key_f, r.x + u(12), base, field.label.c_str(), theme_.text, u(1));
      c.fill(Rect{int(end + u(8)), base - key_f.cap_height() / 2, int(r.right() - u(12) - end - u(8)),
                  std::max(1, int(u(1)))}, theme_.text);
      y += row_h;
      continue;
    }
    if (sel) c.fill(Rect{r.x + int(u(4)), y + 1, r.w - int(u(8)), row_h - 2}, theme_.chip_active);
    const Color ink = sel ? theme_.chip_active_text : theme_.text;
    const Color quiet = sel ? theme_.chip_active_text : theme_.text_secondary;
    const int base = label_f.baseline_in(y, row_h);
    std::string v = field.value();
    if (field.buttons) {
      // "Sel + X, or Menu": each button a keycap, joined by "+" and "or".
      std::string label = field.label;
      for (size_t at; (at = label.find(", or ")) != std::string::npos;) label.replace(at, 5, " or ");
      float x = r.x + u(12);
      const int cap_y = y + int(u(4)), cap_h = row_h - int(u(8));
      size_t start = 0;
      bool first = true;
      while (start <= label.size()) {
        size_t plus = label.find(" + ", start);
        const std::string part = label.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
        if (!first) x = c.text(key_f, x + u(3), base, "+", quiet) + u(3);
        first = false;
        size_t s2 = 0;
        bool first_alt = true;
        while (s2 <= part.size()) {
          size_t orr = part.find(" or ", s2);
          const std::string key = caps(part.substr(s2, orr == std::string::npos ? std::string::npos : orr - s2));
          if (!first_alt) x = c.text(label_f, x + u(3), base, "or", quiet) + u(3);
          first_alt = false;
          x += draw_keycap(c, key_f, int(x), cap_y, cap_h, key.c_str(), ink, ink, true, u(1));
          if (orr == std::string::npos) break;
          s2 = orr + 4;
        }
        if (plus == std::string::npos) break;
        start = plus + 3;
      }
      c.text(action_f, r.right() - u(12) - action_f.measure(v.c_str()), base, v.c_str(), quiet);
    } else {
      const std::string label = caps(field.label);
      c.text(label_f, r.x + u(12), base, label.c_str(), quiet, sp);
      c.text(value_f, r.right() - u(12) - value_f.measure(v.c_str()), base, v.c_str(), ink);
    }
    y += row_h;
  }
  c.set_clip(saved);
}

}  // namespace twill
