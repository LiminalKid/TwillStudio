#include "app/InstrumentView.h"
#include "app/Paper.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app/FactorySounds.h"
#include "engine/PlaitsVoice.h"

namespace twill {

namespace {

// Sizes, in layout units.
constexpr float kRowH = 24, kText = 13, kSmall = 11;

bool is_audio_file(const std::string& name) {
  std::string lower = name;
  for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
  auto ends = [&](const char* ext) {
    const size_t n = std::strlen(ext);
    return lower.size() > n && lower.compare(lower.size() - n, n, ext) == 0;
  };
  return ends(".wav") || ends(".aif") || ends(".aiff");
}

std::string file_name(const std::string& path) {
  size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

const char* const kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
std::string note_name(int n) { return std::string(kNoteNames[n % 12]) + std::to_string(n / 12 - 1); }

// The note a file name ends with ("Pluck C3.wav", "pad_A#2.aif"), as a MIDI note; -1 if none.
int note_in_name(const std::string& path) {
  std::string n = file_name(path);
  n = n.substr(0, n.find_last_of('.'));
  size_t i = n.size();
  while (i > 0 && (std::isdigit(static_cast<unsigned char>(n[i - 1])) || n[i - 1] == '-')) --i;
  if (i == n.size() || i == 0) return -1;
  const int octave = std::atoi(n.c_str() + i);
  bool sharp = false;
  if (n[i - 1] == '#') sharp = true, --i;
  if (i == 0) return -1;
  const char letter = char(std::toupper(static_cast<unsigned char>(n[i - 1])));
  static const int base[7] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G
  if (letter < 'A' || letter > 'G') return -1;
  if (i >= 2 && std::isalpha(static_cast<unsigned char>(n[i - 2]))) return -1;  // part of a word
  const int note = (octave + 1) * 12 + base[letter - 'A'] + (sharp ? 1 : 0);
  return note >= 0 && note <= 127 ? note : -1;
}

}  // namespace

InstrumentView::Row InstrumentView::param_row(ParamId id, const char* label) {
  App& app = app_;
  const int track = app.track();
  const ParamInfo* info = param_info(id);
  Row row;
  row.label = label ? label : info->name;
  row.value = [&app, track, id] { return format_param(id, app.project().track_param(track, id)); };
  row.fraction = [&app, track, id, info] {
    return (app.project().track_param(track, id) - info->min) / (info->max - info->min);
  };
  row.adjust = [&app, track, id, info](int step, bool coarse) {
    float v = app.project().track_param(track, id);
    if (info->integer) v += float(step * (coarse ? (id == kParamRootNote ? 12 : 4) : 1));
    else v += step * (info->max - info->min) * (coarse ? 0.1f : 0.01f);
    app.history().set_track_param(track, id, v);
  };
  row.reset = [&app, track, id, info] { app.history().set_track_param(track, id, info->default_value); };
  return row;
}

std::vector<InstrumentView::Row> InstrumentView::rows() {
  App& app = app_;
  const int track = app.track();
  const Track& t = app.project().tracks[size_t(track)];
  std::vector<Row> out;
  auto change = [&app, track](const std::function<void(Track&)>& f) {
    Track copy = app.project().tracks[size_t(track)];
    f(copy);
    app.history().change_track(track, copy);
  };

  Row inst;
  inst.label = "Engine";  // the synth's engines, then Sampler, then MIDI out
  inst.value = [&app, track] { return std::string(App::sound_name(app.project().tracks[size_t(track)])); };
  inst.fraction = [&app, track] {
    return App::sound_index(app.project().tracks[size_t(track)]) / float(App::kNumSounds - 1);
  };
  inst.adjust = [&app, track](int step, bool) { app.step_sound(track, step); };
  out.push_back(inst);

  if (t.instrument == InstrumentType::MidiOut) {
    Row ch;
    ch.label = "MIDI channel";
    ch.value = [&app, track] { return std::to_string(app.project().tracks[size_t(track)].midi_channel + 1); };
    ch.fraction = [&app, track] { return app.project().tracks[size_t(track)].midi_channel / 15.0f; };
    ch.adjust = [change](int step, bool) {
      change([step](Track& tr) { tr.midi_channel = std::max(0, std::min(15, tr.midi_channel + step)); });
    };
    out.push_back(ch);
    for (int which = 0; which < 2; ++which) {
      Row cc;
      cc.label = which == 0 ? "CC A" : "CC B";
      cc.value = [&app, track, which] { return cc_label(app.project().tracks[size_t(track)].midi_cc[which]); };
      cc.fraction = [&app, track, which] { return app.project().tracks[size_t(track)].midi_cc[which] / 127.0f; };
      cc.adjust = [change, which](int step, bool coarse) {
        change([=](Track& tr) { tr.midi_cc[which] = std::max(0, std::min(127, tr.midi_cc[which] + step * (coarse ? 10 : 1))); });
      };
      cc.reset = [change, which] { change([which](Track& tr) { tr.midi_cc[which] = which == 0 ? 1 : 74; }); };
      out.push_back(cc);
    }
    Row devices;
    devices.label = "Devices";
    devices.value = [&app] { return app.platform().midi() ? app.platform().midi()->describe() : std::string("none"); };
    out.push_back(devices);
    return out;
  }

  if (t.instrument == InstrumentType::Macro) {
    for (ParamId id : {kParamHarmonics, kParamTimbre, kParamMorph, kParamFmAmount, kParamTimbreAmount,
                       kParamMorphAmount})
      out.push_back(param_row(id));
    Row output;
    output.label = "Output";
    output.value = [&app, track] {
      MacroOutput o = app.project().tracks[size_t(track)].macro.output;
      return std::string(o == MacroOutput::Main ? "Main" : o == MacroOutput::Aux ? "Aux" : "Mix");
    };
    output.adjust = [change](int step, bool) {
      change([step](Track& tr) { tr.macro.output = MacroOutput((int(tr.macro.output) + step + 3) % 3); });
    };
    out.push_back(output);
    // The amplifier, then its own settings: the low-pass gate's decay and colour, or the ADSR. The
    // FM, timbre and morph amounts follow whichever it is.
    out.push_back(param_row(kParamAmpMode));
    if (t.tone.amp_mode == 1)
      for (ParamId id : {kParamAttack, kParamDecayTime, kParamSustain, kParamRelease}) out.push_back(param_row(id));
    else
      for (ParamId id : {kParamDecay, kParamLpgColour}) out.push_back(param_row(id));
  } else {
    out.push_back(param_row(kParamSamplerMode));
    if (t.sampler.mode != SamplerMode::Kit) {
      Row sample;
      sample.label = "Sample";
      sample.value = [&app, track] {
        const std::string& s = app.project().tracks[size_t(track)].sampler.sample;
        return s.empty() ? std::string("(none: press A)") : file_name(s);
      };
      sample.press = [this, change, &app, track] {
        const std::string cur = app.project().tracks[size_t(track)].sampler.sample;
        open_browser([change](const std::string& path) {
          change([&](Track& tr) {
            tr.sampler.sample = path;
            // A note at the end of the name ("Pluck C3") is the sample's root.
            const int root = note_in_name(path);
            if (root >= 0) tr.sampler.root = root;
          });
        }, cur);
      };
      sample.reset = [change] { change([](Track& tr) { tr.sampler.sample.clear(); }); };
      out.push_back(sample);
      for (ParamId id : {kParamRootNote, kParamFineTune, kParamSampleStart, kParamSampleEnd, kParamLoopMode})
        out.push_back(param_row(id));
      if (t.sampler.loop != LoopMode::Off) {
        out.push_back(param_row(kParamLoopStart));
        out.push_back(param_row(kParamLoopEnd));
      }
      out.push_back(param_row(kParamReverse));
      if (t.sampler.mode == SamplerMode::Slice) {
        out.push_back(param_row(kParamSliceMode));
        out.push_back(param_row(kParamSlices, t.sampler.slice_transients ? "Most slices" : nullptr));
      }
    } else {
      Row factory;
      factory.label = "Load the factory kit";
      factory.value = [] { return std::string("press A"); };
      factory.press = [change, &app] {
        change([](Track& tr) { load_factory_kit(tr.sampler); });
        app.message("Factory kit loaded");
      };
      out.push_back(factory);
      Row slot;
      slot.label = "Slot";
      slot.value = [this] {
        return std::to_string(kit_slot_ + 1) + "  (" + note_name(kKitFirstNote + kit_slot_) + ")";
      };
      slot.fraction = [this] { return kit_slot_ / float(kKitSlots - 1); };
      slot.adjust = [this](int step, bool coarse) {
        kit_slot_ = std::max(0, std::min(kKitSlots - 1, kit_slot_ + step * (coarse ? 4 : 1)));
      };
      out.push_back(slot);
      const int k = kit_slot_;
      Row ss;
      ss.label = "Slot sample";
      ss.value = [&app, track, k] {
        const std::string& s = app.project().tracks[size_t(track)].sampler.kit[k].sample;
        return s.empty() ? std::string("(empty: press A)") : file_name(s);
      };
      ss.press = [this, change, &app, track, k] {
        const std::string cur = app.project().tracks[size_t(track)].sampler.kit[k].sample;
        open_browser([change, k](const std::string& path) { change([&](Track& tr) { tr.sampler.kit[k].sample = path; }); }, cur);
      };
      ss.reset = [change, k] { change([k](Track& tr) { tr.sampler.kit[k].sample.clear(); }); };
      out.push_back(ss);
      struct SlotField {
        const char* label;
        float KitSlot::*member;
        float lo, hi, step;
      };
      for (SlotField f : {SlotField{"Slot tune", &KitSlot::tune, -24, 24, 1},
                          SlotField{"Slot level", &KitSlot::level, 0, 1, 0.02f},
                          SlotField{"Slot pan", &KitSlot::pan, -1, 1, 0.05f}}) {
        Row r;
        r.label = f.label;
        r.value = [&app, track, k, f] {
          float v = app.project().tracks[size_t(track)].sampler.kit[k].*(f.member);
          char b[24];
          if (f.member == &KitSlot::tune) std::snprintf(b, sizeof b, "%+.0f st", v);
          else if (f.member == &KitSlot::pan) std::snprintf(b, sizeof b, "%+.0f", v * 100);
          else std::snprintf(b, sizeof b, "%.0f%%", v * 100);
          return std::string(b);
        };
        r.fraction = [&app, track, k, f] {
          return (app.project().tracks[size_t(track)].sampler.kit[k].*(f.member) - f.lo) / (f.hi - f.lo);
        };
        r.adjust = [change, k, f](int step, bool coarse) {
          change([&](Track& tr) {
            float& v = tr.sampler.kit[k].*(f.member);
            v = std::max(f.lo, std::min(f.hi, v + step * f.step * (coarse ? (f.member == &KitSlot::tune ? 12 : 5) : 1)));
          });
        };
        out.push_back(r);
      }
      Row choke;
      choke.label = "Choke group";
      choke.value = [&app, track, k] {
        int g = app.project().tracks[size_t(track)].sampler.kit[k].choke;
        return g ? std::to_string(g) : std::string("None");
      };
      choke.adjust = [change, k](int step, bool) {
        change([&](Track& tr) { tr.sampler.kit[k].choke = std::max(0, std::min(4, tr.sampler.kit[k].choke + step)); });
      };
      out.push_back(choke);
      Row decay;
      decay.label = "Slot decay";
      decay.value = [&app, track, k] {
        const float secs = kit_decay_seconds(app.project().tracks[size_t(track)].sampler.kit[k].decay);
        if (secs <= 0) return std::string("Full");
        char b[24];
        if (secs < 1) std::snprintf(b, sizeof b, "%.0f ms", secs * 1000);
        else std::snprintf(b, sizeof b, "%.1f s", secs);
        return std::string(b);
      };
      decay.fraction = [&app, track, k] { return app.project().tracks[size_t(track)].sampler.kit[k].decay; };
      decay.adjust = [change, k](int step, bool coarse) {
        change([&](Track& tr) {
          float& v = tr.sampler.kit[k].decay;
          v = std::max(0.0f, std::min(1.0f, v + step * (coarse ? 0.1f : 0.02f)));
        });
      };
      decay.reset = [change, k] { change([k](Track& tr) { tr.sampler.kit[k].decay = 1; }); };
      out.push_back(decay);
    }
    for (ParamId id : {kParamAttack, kParamDecayTime, kParamSustain, kParamRelease}) out.push_back(param_row(id));
  }
  for (ParamId id : {kParamFilterType, kParamCutoff, kParamResonance, kParamDrive}) out.push_back(param_row(id));
  Row voices;
  voices.label = t.instrument == InstrumentType::Sampler && t.sampler.mode == SamplerMode::Kit ? "Voices per drum" : "Voices";
  voices.value = [&app, track] { return voices_label(app.project().tracks[size_t(track)].voices); };
  voices.adjust = [&app, track](int step, bool) {
    const Track& tr = app.project().tracks[size_t(track)];
    app.history().set_track_flags(track, tr.mute, tr.solo, tr.voices + step);
  };
  out.push_back(voices);

  // Modulators.
  const std::vector<ParamId> dests = mod_destinations(t.instrument);
  for (int i = 0; i < kModulators; ++i) {
    const Modulator& m = t.mods[i];
    auto mod_change = [change, i](const std::function<void(Modulator&)>& f) { change([&](Track& tr) { f(tr.mods[i]); }); };
    Row type;
    type.label = "Modulator " + std::to_string(i + 1);
    type.value = [&app, track, i] { return std::string(mod_type_name(app.project().tracks[size_t(track)].mods[i].type)); };
    type.adjust = [mod_change, dests](int step, bool) {
      mod_change([&](Modulator& mm) {
        mm.type = ModType((int(mm.type) + step + 5) % 5);
        if (mm.dest == kParamNone || std::find(dests.begin(), dests.end(), mm.dest) == dests.end()) mm.dest = dests.size() > 1 ? dests[1] : dests[0];
      });
    };
    type.reset = [mod_change] { mod_change([](Modulator& mm) { mm = Modulator{}; }); };
    out.push_back(type);
    if (m.type == ModType::Off) continue;
    Row dest;
    dest.label = "    to";
    dest.value = [&app, track, i] {
      const ParamInfo* info = param_info(app.project().tracks[size_t(track)].mods[i].dest);
      return std::string(info ? info->name : "-");
    };
    dest.adjust = [mod_change, dests](int step, bool) {
      mod_change([&](Modulator& mm) {
        auto it = std::find(dests.begin(), dests.end(), mm.dest);
        int idx = it == dests.end() ? 0 : int(it - dests.begin());
        mm.dest = dests[size_t((idx + step + int(dests.size())) % int(dests.size()))];
      });
    };
    out.push_back(dest);
    Row depth;
    depth.label = "    depth";
    depth.value = [&app, track, i] {
      char b[16];
      std::snprintf(b, sizeof b, "%+.0f%%", app.project().tracks[size_t(track)].mods[i].depth * 100);
      return std::string(b);
    };
    depth.fraction = [&app, track, i] { return (app.project().tracks[size_t(track)].mods[i].depth + 1) / 2; };
    depth.adjust = [mod_change](int step, bool coarse) {
      mod_change([&](Modulator& mm) { mm.depth = std::max(-1.0f, std::min(1.0f, mm.depth + step * (coarse ? 0.1f : 0.01f))); });
    };
    out.push_back(depth);
    auto unit_row = [&](const char* label, float Modulator::*member, bool time) {
      Row r;
      r.label = label;
      r.value = [&app, track, i, member, time] {
        float v = app.project().tracks[size_t(track)].mods[i].*member;
        if (time) return format_param(kParamAttack, v);
        return lfo_rate_text(app.project().tracks[size_t(track)].mods[i]);
      };
      r.fraction = [&app, track, i, member] { return app.project().tracks[size_t(track)].mods[i].*member; };
      r.adjust = [mod_change, member](int step, bool coarse) {
        mod_change([&](Modulator& mm) { mm.*member = std::max(0.0f, std::min(1.0f, mm.*member + step * (coarse ? 0.1f : 0.01f))); });
      };
      return r;
    };
    if (m.type == ModType::Envelope) {
      out.push_back(unit_row("    attack", &Modulator::attack, true));
      out.push_back(unit_row("    decay", &Modulator::decay, true));
    } else if (m.type == ModType::Lfo) {
      Row shape;
      shape.label = "    shape";
      shape.value = [&app, track, i] { return std::string(lfo_shape_name(app.project().tracks[size_t(track)].mods[i].shape)); };
      shape.adjust = [mod_change](int step, bool) { mod_change([&](Modulator& mm) { mm.shape = LfoShape((int(mm.shape) + step + 5) % 5); }); };
      out.push_back(shape);
      Row rate = unit_row("    rate", &Modulator::rate, false);
      if (m.sync)  // synced rates are steps: 4 bars ... 1/16
        rate.adjust = [mod_change](int step, bool) {
          mod_change([&](Modulator& mm) { mm.rate = std::max(0.0f, std::min(1.0f, std::round(mm.rate * 6 + step) / 6)); });
        };
      out.push_back(rate);
      Row sync;
      sync.label = "    sync to tempo";
      sync.value = [&app, track, i] { return std::string(app.project().tracks[size_t(track)].mods[i].sync ? "On" : "Off"); };
      sync.adjust = [mod_change](int, bool) { mod_change([](Modulator& mm) { mm.sync = !mm.sync; }); };
      out.push_back(sync);
      // Each note: restarts with every note. Otherwise one LFO for the whole track, running across
      // notes: free from when playback starts, or (synced) following the song position.
      Row phase;
      phase.label = "    phase";
      phase.value = [&app, track, i] {
        const Modulator& mm = app.project().tracks[size_t(track)].mods[i];
        return std::string(mm.retrigger ? "Each note" : mm.sync ? "Follow song" : "Free run");
      };
      phase.adjust = [mod_change](int, bool) { mod_change([](Modulator& mm) { mm.retrigger = !mm.retrigger; }); };
      out.push_back(phase);
    }
  }
  return out;
}

int InstrumentView::audition_note() const {
  const Track& t = app_.project().tracks[size_t(app_.track())];
  if (t.instrument == InstrumentType::Sampler) {
    if (t.sampler.mode == SamplerMode::Kit) return kKitFirstNote + kit_slot_;
    return t.sampler.root;
  }
  return 60;
}

// --- Browser ----------------------------------------------------------------------------------

void InstrumentView::open_browser(std::function<void(const std::string&)> on_pick, const std::string& start) {
  Platform& p = app_.platform();
  on_pick_ = std::move(on_pick);
  // Start in the current sample's folder, else the app's samples folder.
  std::string dir = p.data_dir() + "samples/";
  if (!start.empty()) {
    std::string full = start[0] == '/' ? start : p.data_dir() + start;
    dir = full.substr(0, full.find_last_of('/') + 1);
  }
  const std::string select = start.empty() ? std::string() : file_name(start);
  if (!list_browser_dir(dir, select)) list_browser_dir(p.data_dir(), "");
  browser_open_ = true;
  app_.invalidate_all();
}

// Folders and audio files in `dir`, after an "Up a folder" entry (".."), selecting `select`.
bool InstrumentView::list_browser_dir(const std::string& dir, const std::string& select) {
  std::vector<Platform::DirEntry> e;
  if (!app_.platform().list_dir(dir.c_str(), e)) return false;
  e.erase(std::remove_if(e.begin(), e.end(),
                         [](const Platform::DirEntry& d) { return !d.is_dir && !is_audio_file(d.name); }),
          e.end());
  if (dir != "/") e.insert(e.begin(), Platform::DirEntry{"..", true});
  dir_ = dir;
  entries_ = std::move(e);
  bsel_ = btop_ = 0;
  for (size_t i = 0; i < entries_.size(); ++i)
    if (!select.empty() && entries_[i].name == select) bsel_ = int(i);
  // Otherwise the first real entry, so A doesn't go straight back up.
  if (select.empty() && entries_.size() > 1 && entries_[0].name == "..") bsel_ = 1;
  return true;
}

void InstrumentView::browser_input() {
  const InputTracker& in = app_.input();
  Platform& p = app_.platform();
  auto close = [&] {
    browser_open_ = false;
    app_.engine().preview(nullptr);
    app_.invalidate_all();
  };
  auto go = [&](const std::string& dir, const std::string& select) {
    if (!list_browser_dir(dir, select)) app_.message("Can't open that folder");
  };
  auto up = [&] {  // to the parent, with the folder we came from selected
    std::string parent = dir_.substr(0, dir_.size() - 1);
    std::string name = parent.substr(parent.find_last_of('/') + 1);
    parent = parent.substr(0, parent.find_last_of('/') + 1);
    go(parent.empty() ? "/" : parent, name);
  };
  const int n = int(entries_.size());
  if (in.pressed(kX)) return close();
  if (n > 0 && in.pressed_or_repeat(kUp)) bsel_ = (bsel_ + n - 1) % n;
  if (n > 0 && in.pressed_or_repeat(kDown)) bsel_ = (bsel_ + 1) % n;
  if (in.pressed(kB)) {
    if (dir_ == "/") return close();
    up();
  } else if (n > 0 && (in.pressed(kA) || in.pressed(kY))) {
    const Platform::DirEntry e = entries_[size_t(bsel_)];  // a copy: going to a folder replaces the list
    if (e.is_dir) {
      if (in.pressed(kA)) e.name == ".." ? up() : go(dir_ + e.name + "/", "");
    } else {
      std::string full = dir_ + e.name;
      // Store paths inside the data folder relative to it, so projects move with the folder.
      const std::string data = p.data_dir();
      std::string path = full.compare(0, data.size(), data) == 0 ? full.substr(data.size()) : full;
      if (in.pressed(kY)) {
        std::string err;
        preview_ = app_.load_sample(path, &err);
        app_.engine().preview(preview_.get());
        if (!preview_) app_.message(err);
      } else {
        std::string err;
        if (!app_.load_sample(path, &err)) {
          app_.message(err);
        } else {
          on_pick_(path);
          close();
          return;
        }
      }
    }
  }
  app_.invalidate(app_.content_rect());
}

// --- Input ------------------------------------------------------------------------------------

void InstrumentView::handle_input() {
  if (browser_open_) return browser_input();
  const InputTracker& in = app_.input();
  std::vector<Row> list = rows();
  const int n = int(list.size());
  sel_ = std::max(0, std::min(sel_, n - 1));
  bool changed = false;

  // X: the track's settings, as on its header in the arrangement.
  if (in.pressed(kX)) {
    app_.open_track_settings(app_.track());
    return;
  }
  if (in.pressed_or_repeat(kUp)) sel_ = (sel_ + n - 1) % n, changed = true;
  if (in.pressed_or_repeat(kDown)) sel_ = (sel_ + 1) % n, changed = true;
  const int step = in.pressed_or_repeat(kRight) ? 1 : in.pressed_or_repeat(kLeft) ? -1 : 0;
  if (step && list[size_t(sel_)].adjust) {
    list[size_t(sel_)].adjust(step, in.held(kR));
    changed = true;
  }
  if (in.pressed(kA) && list[size_t(sel_)].press) {
    list[size_t(sel_)].press();
    return;
  }
  if (in.pressed(kB) && list[size_t(sel_)].reset) list[size_t(sel_)].reset(), changed = true;
  if (in.pressed(kY)) app_.audition(app_.track(), audition_note());
  if (changed || in.pressed(~0u)) app_.invalidate(app_.content_rect());
}

std::vector<Hint> InstrumentView::hints() const {
  if (browser_open_) return {{"A", "open/pick"}, {"Y", "listen"}, {"B", "up a folder"}, {"X", "cancel"}};
  return {{"Up/Down", "choose"}, {"Left/Right", "change"}, {"A", "browse"}, {"B", "reset"}, {"Y", "listen"},
          {"X", "track"}};
}

// --- Drawing ----------------------------------------------------------------------------------

void InstrumentView::draw(Canvas& c) {
  const Theme& th = app_.theme();
  const Rect content = app_.content_rect();
  if (browser_open_) return draw_browser(c, content);
  const Track& t = app_.project().tracks[size_t(app_.track())];
  std::vector<Row> list = rows();
  const Font& f = Font::get(FontFace::CondensedSemiBold, app_.u(kText));
  const Font& fb = Font::get(FontFace::CondensedBold, app_.u(kText + 2));
  const float sp = app_.u(kCapsSpacing);

  // Settings list on the left; the selected row an ink block.
  const int list_w = content.w * 52 / 100;
  Rect list_r{content.x, content.y, list_w, content.h};
  c.fill(list_r, th.background);
  const int row_h = int(app_.u(kRowH));
  const int visible = std::max(1, (list_r.h - int(app_.u(8))) / row_h);
  if (sel_ < top_) top_ = sel_;
  if (sel_ >= top_ + visible) top_ = sel_ - visible + 1;
  int y = list_r.y + int(app_.u(4));
  const Color col = app_.track_colour(app_.track());
  for (int i = top_; i < int(list.size()) && i < top_ + visible; ++i, y += row_h) {
    const Row& row = list[size_t(i)];
    const bool sel = i == sel_;
    Rect rr{list_r.x + int(app_.u(4)), y, list_r.w - int(app_.u(8)), row_h};
    if (sel) c.fill(Rect{rr.x, rr.y + 1, rr.w, rr.h - 2}, th.chip_active);
    const int base = f.baseline_in(y, row_h - (row.fraction ? int(app_.u(3)) : 0));
    const std::string label = caps(row.label);
    c.text(f, rr.x + app_.u(8), base, label.c_str(), sel ? th.chip_active_text : th.text_secondary, sp);
    std::string v = row.value ? row.value() : "";
    c.text(fb, rr.right() - app_.u(8) - fb.measure(v.c_str()), base, v.c_str(), sel ? th.chip_active_text : th.text);
    if (row.fraction) {
      const float fr = std::max(0.0f, std::min(1.0f, row.fraction()));
      const int bw = int(app_.u(64));
      Rect bar{rr.right() - int(app_.u(8)) - bw, y + row_h - int(app_.u(5)), bw, std::max(2, int(app_.u(2)))};
      c.fill(bar, sel ? th.text_secondary : th.grid_bar);
      c.fill(Rect{bar.x, bar.y, int(bw * fr), bar.h}, col);
    }
  }
  if (y < list_r.bottom()) c.fill(Rect{list_r.x, y, list_r.w, list_r.bottom() - y}, th.background);
  c.fill(Rect{list_r.right(), content.y, std::max(1, int(app_.u(1))), content.h}, th.text);

  // Picture on the right.
  const int rule = std::max(1, int(app_.u(1)));
  Rect pic{list_r.right() + rule, content.y, content.right() - list_r.right() - rule, content.h};
  c.fill(pic, th.background);
  if (t.instrument == InstrumentType::MidiOut) {
    const Font& big = Font::get(FontFace::CondensedBold, app_.u(26));
    const Font& sm = Font::get(FontFace::Regular, app_.u(kText));
    const std::string ch = "Channel " + std::to_string(t.midi_channel + 1);
    c.text(big, pic.x + app_.u(16), pic.y + int(app_.u(70)), "MIDI OUT", th.track_ink(int(t.colour)));
    c.text(sm, pic.x + app_.u(16), pic.y + int(app_.u(96)), ch.c_str(), th.text_secondary);
    for (int which = 0; which < 2; ++which) {
      const std::string cc = std::string(which == 0 ? "CC A: " : "CC B: ") + cc_label(t.midi_cc[which]);
      c.text(sm, pic.x + app_.u(16), pic.y + int(app_.u(116 + 18 * which)), cc.c_str(), th.text_secondary);
    }
  } else if (t.instrument == InstrumentType::Macro) {
    draw_macro(c, pic, t);
  } else if (t.sampler.mode == SamplerMode::Kit) {
    draw_kit(c, pic, t);
  } else {
    std::shared_ptr<const SampleData> s = t.sampler.sample.empty() ? nullptr : app_.load_sample(t.sampler.sample);
    if (s) {
      wave_hold_ = s;
      draw_waveform(c, pic, *s, t);
    } else {
      const Font& sm = Font::get(FontFace::Regular, app_.u(kSmall + 1));
      const char* msg = t.sampler.sample.empty() ? "No sample yet" : "Sample not found";
      c.text(sm, pic.x + (pic.w - sm.measure(msg)) / 2, pic.y + pic.h / 2, msg, th.text_secondary);
    }
  }
}

void InstrumentView::draw_macro(Canvas& c, const Rect& r, const Track& t) {
  const Theme& th = app_.theme();
  const Font& big = Font::get(FontFace::CondensedBold, app_.u(26));
  const Font& label = Font::get(FontFace::CondensedSemiBold, app_.u(kText));
  const Font& value = Font::get(FontFace::CondensedBold, app_.u(kText + 2));
  const float sp = app_.u(0.6f);
  const int x = r.x + int(app_.u(16));
  const int w = r.w - int(app_.u(32));
  int y = r.y + int(app_.u(26));
  char num[16];
  std::snprintf(num, sizeof num, "ENGINE %d", t.macro.engine + 1);
  c.text(label, x, y, num, th.text_secondary, sp);
  y += int(app_.u(30));
  // The engine's name, large, shrinking to fit.
  std::string name = caps(PlaitsVoice::engine_name(t.macro.engine));
  const Font* nf = &big;
  if (nf->measure(name.c_str()) > w) nf = &Font::get(FontFace::CondensedBold, app_.u(20));
  while (name.size() > 1 && nf->measure(name.c_str()) > w) name.pop_back();
  c.text(*nf, x, y, name.c_str(), th.track_ink(int(t.colour)));
  y += int(app_.u(32));
  // The three main controls, large: a thick bar in the track's colour with an ink mark at the value.
  const Color col = app_.track_colour(app_.track());
  for (auto [name_c, v] : {std::pair<const char*, float>{"HARMONICS", t.macro.harmonics},
                           {"TIMBRE", t.macro.timbre}, {"MORPH", t.macro.morph}}) {
    c.text(label, x, y, name_c, th.text_secondary, sp);
    char vb[16];
    std::snprintf(vb, sizeof vb, "%.2f", v);
    c.text(value, x + w - value.measure(vb), y, vb, th.text);
    Rect bar{x, y + int(app_.u(6)), w, int(app_.u(10))};
    c.fill(bar, th.grid_step);
    c.fill(Rect{bar.x, bar.y, int(bar.w * v), bar.h}, col);
    const int mw = std::max(2, int(app_.u(2)));
    c.fill(Rect{bar.x + int(bar.w * v) - mw / 2, bar.y - int(app_.u(2)), mw, bar.h + int(app_.u(4))}, th.text);
    y += int(app_.u(44));
  }
}

void InstrumentView::draw_waveform(Canvas& c, const Rect& r, const SampleData& s, const Track& t) {
  const Theme& th = app_.theme();
  const Font& f = Font::get(FontFace::Regular, app_.u(kSmall + 1));
  const Font& fb = Font::get(FontFace::SemiBold, app_.u(kText));
  const Color col = app_.track_colour(app_.track());
  Rect wave{r.x + int(app_.u(12)), r.y + int(app_.u(54)), r.w - int(app_.u(24)), r.h - int(app_.u(84))};
  c.text(fb, wave.x, r.y + int(app_.u(22)), s.name.c_str(), th.text);
  char info[64];
  std::snprintf(info, sizeof info, "%.2f s   %d Hz   %s", s.frames / 48000.0, s.source_rate,
                s.source_channels == 1 ? "mono" : "stereo");
  c.text(f, wave.x, r.y + int(app_.u(40)), info, th.text_secondary);

  // Peaks per column, cached.
  if (wave_for_ != &s || wave_width_ != wave.w) {
    wave_for_ = &s;
    wave_width_ = wave.w;
    wave_peaks_.assign(size_t(wave.w), 0);
    for (int x = 0; x < wave.w; ++x) {
      const int a = int(int64_t(x) * s.frames / wave.w), b = std::max(a + 1, int(int64_t(x + 1) * s.frames / wave.w));
      float pk = 0;
      for (int i = a; i < b && i < s.frames; ++i) pk = std::max(pk, std::max(std::fabs(s.left(i)), std::fabs(s.right(i))));
      wave_peaks_[size_t(x)] = pk;
    }
  }
  c.fill(wave, th.panel);
  const SamplerSettings& sm = t.sampler;
  // Loop region tint.
  if (sm.loop != LoopMode::Off) {
    int a = wave.x + int(sm.loop_start * wave.w), b = wave.x + int(sm.loop_end * wave.w);
    c.fill(Rect{a, wave.y, b - a, wave.h}, col.with_alpha(28));
  }
  const int mid = wave.y + wave.h / 2;
  for (int x = 0; x < wave.w; ++x) {
    const int h = int(wave_peaks_[size_t(x)] * (wave.h / 2 - 2));
    const float fr = float(x) / wave.w;
    const bool inside = fr >= sm.start && fr <= sm.end;
    c.vline(wave.x + x, mid - h, mid + h + 1, inside ? col : th.grid_bar);
  }
  // Start, end, loop and slice markers.
  auto marker = [&](float fr, Color mc) { c.fill(Rect{wave.x + int(fr * wave.w), wave.y, std::max(2, int(app_.u(1.5f))), wave.h}, mc); };
  if (sm.mode == SamplerMode::Slice && sm.slice_transients) {
    const std::vector<int> pts = transient_slices(s, sm.start, sm.end, sm.slices);
    for (size_t i = 1; i + 1 < pts.size(); ++i) marker(float(pts[i]) / s.frames, th.grid_bar);
  } else if (sm.mode == SamplerMode::Slice) {
    for (int i = 1; i < sm.slices; ++i) marker(sm.start + (sm.end - sm.start) * i / sm.slices, th.grid_bar);
  }
  if (sm.loop != LoopMode::Off) {
    marker(sm.loop_start, col);
    marker(sm.loop_end, col);
  }
  marker(sm.start, th.text);
  marker(sm.end, th.text);
  c.stroke_rect(wave, 1, th.divider);
}

void InstrumentView::draw_kit(Canvas& c, const Rect& r, const Track& t) {
  const Theme& th = app_.theme();
  const Font& f = Font::get(FontFace::CondensedSemiBold, app_.u(kSmall + 1));
  const Font& fb = Font::get(FontFace::CondensedBold, app_.u(15));
  const float sp = 0;  // pads are narrow: no letter spacing
  const int pad = int(app_.u(10)), gap = int(app_.u(6));
  const int cols = 4, rows = 4;
  const int cw = (r.w - 2 * pad - (cols - 1) * gap) / cols, ch = (r.h - 2 * pad - (rows - 1) * gap) / rows;
  const Color fill = th.track_fill(int(t.colour)), ink = th.track_ink(int(t.colour));
  const int line_h = int(app_.u(13));
  for (int i = 0; i < kKitSlots; ++i) {
    const int cx = r.x + pad + (i % cols) * (cw + gap), cy = r.y + pad + (i / cols) * (ch + gap);
    Rect cell{cx, cy, cw, ch};
    const bool has = !t.sampler.kit[i].sample.empty();
    if (has) c.fill(cell, fill);
    else c.stroke_rect(cell, std::max(1, int(app_.u(1))), th.divider);
    c.text(fb, cx + app_.u(5), cy + int(app_.u(16)), std::to_string(i + 1).c_str(), has ? ink : th.text_secondary);
    if (has) {
      std::string name = file_name(t.sampler.kit[i].sample);
      name = caps(name.substr(0, name.find_last_of('.')));
      if (name.size() > 3 && std::isdigit(static_cast<unsigned char>(name[0])) && name[2] == ' ') name = name.substr(3);
      // One line if it fits, else two (split at the last space that lets the first line fit),
      // shortened if a word is still too long.
      const float room = cw - app_.u(8);
      std::vector<std::string> lines{name};
      if (f.measure(name.c_str(), sp) > room) {
        size_t cut = name.rfind(' ');
        while (cut != std::string::npos && cut > 0 && f.measure(name.substr(0, cut).c_str(), sp) > room)
          cut = name.rfind(' ', cut - 1);
        if (cut != std::string::npos && cut > 0) lines = {name.substr(0, cut), name.substr(cut + 1)};
      }
      for (std::string& ln : lines)
        while (ln.size() > 1 && f.measure(ln.c_str(), sp) > room) ln.pop_back();
      int by = cy + ch - int(app_.u(6)) - int(lines.size() - 1) * line_h;
      for (const std::string& ln : lines) {
        c.text(f, cx + app_.u(5), by, ln.c_str(), ink, sp);
        by += line_h;
      }
    }
  }
  // The selected slot: viewfinder corners.
  const int i = kit_slot_;
  if (i >= 0 && i < kKitSlots)
    draw_brackets(c, Rect{r.x + pad + (i % cols) * (cw + gap), r.y + pad + (i / cols) * (ch + gap), cw, ch}, th.text,
                  app_.u(1));
}

void InstrumentView::draw_browser(Canvas& c, const Rect& r) {
  const Theme& th = app_.theme();
  const Font& f = Font::get(FontFace::Regular, app_.u(kText));
  const Font& fb = Font::get(FontFace::SemiBold, app_.u(kText));
  const Font& small = Font::get(FontFace::Regular, app_.u(kSmall));
  c.fill(r, th.panel);
  // The folder, shortened from the left to fit.
  std::string shown = dir_;
  while (shown.size() > 4 && small.measure(shown.c_str()) > r.w - app_.u(24)) shown = "..." + shown.substr(4);
  c.fill(Rect{r.x, r.y, r.w, int(app_.u(24))}, th.header);
  c.text(small, r.x + app_.u(12), r.y + int(app_.u(16)), shown.c_str(), th.text_secondary);
  const int row_h = int(app_.u(kRowH));
  const int top_y = r.y + int(app_.u(28));
  const int visible = std::max(1, (r.bottom() - top_y) / row_h);
  if (bsel_ < btop_) btop_ = bsel_;
  if (bsel_ >= btop_ + visible) btop_ = bsel_ - visible + 1;
  const bool empty = entries_.empty() || (entries_.size() == 1 && entries_[0].name == "..");
  if (empty) c.text(f, r.x + app_.u(16), top_y + int(entries_.size() + 1) * row_h, "No folders or audio files here", th.text_secondary);
  for (int i = btop_; i < int(entries_.size()) && i < btop_ + visible; ++i) {
    const int y = top_y + (i - btop_) * row_h;
    Rect rr{r.x + int(app_.u(6)), y, r.w - int(app_.u(12)), row_h};
    const bool sel = i == bsel_;
    if (sel) c.fill(Rect{rr.x, rr.y + 1, rr.w, rr.h - 2}, th.chip_active);
    const auto& e = entries_[size_t(i)];
    const int base = f.baseline_in(y, row_h);
    std::string label = e.name == ".." ? std::string("Up a folder") : e.is_dir ? e.name + "/" : e.name;
    c.text(e.is_dir ? fb : f, rr.x + app_.u(10), base, label.c_str(),
           sel ? th.chip_active_text : e.is_dir ? th.text : th.text_secondary);
  }
}

}  // namespace twill
