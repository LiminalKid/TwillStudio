#include "app/InstrumentView.h"
#include "app/Paper.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

#include "app/FactorySounds.h"
#include "engine/PlaitsVoice.h"
#include "engine/Sample.h"

namespace twill {

namespace {

// Sizes, in layout units.
constexpr float kRowH = 24, kText = 13, kSmall = 11;

bool ends_with(const std::string& name, const char* ext) {
  std::string lower = name;
  for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
  const size_t n = std::strlen(ext);
  return lower.size() > n && lower.compare(lower.size() - n, n, ext) == 0;
}

bool is_audio_file(const std::string& name) {
  return ends_with(name, ".wav") || ends_with(name, ".aif") || ends_with(name, ".aiff") || ends_with(name, ".flac");
}


// A DecentSampler library folder ("Basic Piano.dsbundle"): its presets, its samples and images.
bool is_bundle(const std::string& name) {
  std::string n = name;
  while (!n.empty() && n.back() == '/') n.pop_back();
  return ends_with(n, ".dsbundle");
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
  // Named steps where the engine reads the setting in steps (format_track_param).
  row.value = [&app, track, id] {
    return format_track_param(app.project().tracks[size_t(track)], id, app.project().track_param(track, id));
  };
  row.fraction = [&app, track, id, info] {
    return (app.project().track_param(track, id) - info->min) / (info->max - info->min);
  };
  row.adjust = [&app, track, id](int step, bool coarse) {
    const float v = app.project().track_param(track, id);
    app.history().set_track_param(track, id, step_track_param(app.project().tracks[size_t(track)], id, v, step, coarse, 0.01f));
  };
  row.reset = [&app, track, id, info] { app.history().set_track_param(track, id, info->default_value); };
  row.param = id;
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

#if TWILL_PLUGINS
  if (t.instrument == InstrumentType::Plugin) {
    Row pick;
    pick.label = "Plugin";
    pick.value = [&app, track] {
      const PluginSettings& pl = app.project().tracks[size_t(track)].plugin;
      return pl.name.empty() ? std::string("None: A to choose") : pl.name;
    };
    pick.press = [this] { open_plugins(); };
    out.push_back(pick);
    PluginInstance* inst = app.plugin_instance(t);
    if (!inst) {
      const std::string why = app.plugin_error(t);
      if (!why.empty()) {
        Row status;
        status.label = "Not loaded";
        status.value = [why] { return why; };
        out.push_back(status);
      }
      return out;
    }
    // A plugin made for the handheld draws a screen of its own: A shows it, Menu comes back.
    if (inst->has_screen()) {
      Row screen;
      screen.label = "Its screen";
      screen.value = [] { return std::string("press A, Menu returns"); };
      screen.press = [&app, track] { app.open_plugin_screen(track); };
      out.push_back(screen);
    }
    // Its parameters, read once for the rows; each moves it at once (not an edit: its state is the
    // plugin's, saved with the song).
    auto ps = std::make_shared<std::vector<PluginInstance::Param>>(inst->params());
    for (size_t i = 0; i < ps->size() && i < 64; ++i) {
      Row row;
      row.label = (*ps)[i].name;
      row.value = [ps, i, inst] {
        const PluginInstance::Param& q = (*ps)[i];
        std::string v = inst->param_text(q.id, q.value);
        if (v.empty()) {
          char b[32];
          std::snprintf(b, sizeof b, q.stepped ? "%.0f" : "%.2f", q.value);
          v = b;
        }
        return v;
      };
      row.fraction = [ps, i] {
        const PluginInstance::Param& q = (*ps)[i];
        return q.max > q.min ? float((q.value - q.min) / (q.max - q.min)) : 0.0f;
      };
      row.adjust = [&app, ps, i, inst](int step, bool coarse) {
        const PluginInstance::Param& q = (*ps)[i];
        const double range = q.max - q.min;
        const double by = q.stepped ? (coarse ? std::max(1.0, std::round(range / 10)) : 1.0) : range * (coarse ? 0.1 : 0.01);
        inst->set_param(q.id, std::max(q.min, std::min(q.max, q.value + step * by)));
        app.plugin_changed();
      };
      row.reset = [&app, ps, i, inst] {
        inst->set_param((*ps)[i].id, (*ps)[i].def);
        app.plugin_changed();
      };
      out.push_back(row);
    }
    return out;
  }
#endif
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
                       kParamMorphAmount}) {
      Row row = param_row(id);
      out.push_back(row);
    }
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
    if (t.macro.engine == 7) {  // chiptune: how fast a held note's arpeggio steps
      Row arp;
      arp.label = "Arpeggio";
      arp.value = [&app, track] { return std::string(arp_rate_name(app.project().tracks[size_t(track)].macro.arp_rate)); };
      arp.fraction = [&app, track] { return int(app.project().tracks[size_t(track)].macro.arp_rate) / float(kNumArpRates - 1); };
      arp.adjust = [change](int step, bool) {
        change([step](Track& tr) {
          tr.macro.arp_rate = ArpRate(std::max(0, std::min(kNumArpRates - 1, int(tr.macro.arp_rate) + step)));
        });
      };
      arp.reset = [change] { change([](Track& tr) { tr.macro.arp_rate = ArpRate::Sixteenth; }); };
      out.push_back(arp);
    }
    // The amplifier, then its own settings: the low-pass gate's decay and colour, or the ADSR. The
    // FM, timbre and morph amounts follow whichever it is.
    out.push_back(param_row(kParamAmpMode));
    if (t.tone.amp_mode == 1)
      for (ParamId id : {kParamAttack, kParamDecayTime, kParamSustain, kParamRelease}) out.push_back(param_row(id));
    else
      for (ParamId id : {kParamDecay, kParamLpgColour}) out.push_back(param_row(id));
  } else {
    out.push_back(param_row(kParamSamplerMode));
    if (t.sampler.mode == SamplerMode::Multi) {
      // A multisample: its preset, what's in it, and what of it isn't played.
      Row preset;
      preset.label = "Preset";
      preset.value = [&app, track] {
        const std::string& s = app.project().tracks[size_t(track)].sampler.preset;
        if (s.empty()) return std::string("(none: press A)");
        std::string name = file_name(s);
        return name.substr(0, name.find_last_of('.'));
      };
      preset.press = [this, &app, track] {
        // It loads in the background; the track takes it once it's ready (App::pick_multisample).
        open_browser([&app, track](const std::string& path) { app.pick_multisample(track, path); },
                     app.project().tracks[size_t(track)].sampler.preset, Browse::Presets);
      };
      preset.reset = [change] { change([](Track& tr) { tr.sampler.preset.clear(); }); };
      out.push_back(preset);
      if (!t.sampler.preset.empty()) {
        Row zones;
        zones.label = "Zones";
        zones.value = [&app, track] {
          const std::string& path = app.project().tracks[size_t(track)].sampler.preset;
          std::string err;
          if (const Multisample* m = app.load_multisample(path, &err)) {
            char b[96];
            std::snprintf(b, sizeof b, "%zu, %s to %s", m->zones.size(), note_name(m->lowest_key()).c_str(),
                          note_name(m->highest_key()).c_str());
            std::string text = b;
            if (const int missing = app.multisample_missing(path)) text += ", " + std::to_string(missing) + " missing";
            return text;
          }
          if (app.multisample_loading(path)) return std::string("Loading...");
          return err.empty() ? std::string("Can't load") : "Can't load: " + err;
        };
        out.push_back(zones);
      }
      out.push_back(param_row(kParamFineTune));
    } else if (t.sampler.mode != SamplerMode::Kit) {
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
      if (app.platform().capture_available()) {
        Row rec;
        rec.label = "Record";
        rec.value = [] { return std::string("press A"); };
        rec.press = [this] { open_recorder(-1); };
        out.push_back(rec);
      }
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
      factory.press = [change, &app, track] {
        // A kit of samples that differs from the factory one would lose its changes: ask first.
        const SamplerSettings& now = app.project().tracks[size_t(track)].sampler;
        SamplerSettings factory_kit = now;
        load_factory_kit(factory_kit);
        bool has_samples = false;
        for (const KitSlot& k : now.kit) has_samples |= !k.sample.empty();
        if (has_samples && !(factory_kit == now) &&
            !app.confirm_twice("load the factory kit", "Press A again to replace this kit with the factory kit"))
          return;
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
      if (app.platform().capture_available()) {
        Row rec;
        rec.label = "Record into slot";
        rec.value = [] { return std::string("press A"); };
        rec.press = [this, k] { open_recorder(k); };
        out.push_back(rec);
      }
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

void InstrumentView::open_browser(std::function<void(const std::string&)> on_pick, const std::string& start,
                                  Browse what) {
  Platform& p = app_.platform();
  on_pick_ = std::move(on_pick);
  browse_ = what;
  // Start in the current sample's folder, else the app's samples folder.
  std::string dir = p.data_dir() + "samples/";
  if (!start.empty()) {
    std::string full = start[0] == '/' ? start : p.data_dir() + start;
    dir = full.substr(0, full.find_last_of('/') + 1);
  }
  std::string select = start.empty() ? std::string() : file_name(start);
  // A preset in a one-preset bundle is listed in the bundle's folder, as the bundle.
  if (what == Browse::Presets && is_bundle(dir)) {
    std::vector<Platform::DirEntry> inside;
    int count = 0;
    if (p.list_dir(dir.c_str(), inside))
      for (const Platform::DirEntry& f : inside) count += !f.is_dir && is_multisample_file(f.name);
    if (count == 1) {
      const std::string bundle = dir.substr(0, dir.size() - 1);
      select = file_name(bundle) + "/" + select;
      dir = bundle.substr(0, bundle.find_last_of('/') + 1);
    }
  }
  if (!list_browser_dir(dir, select)) list_browser_dir(p.data_dir(), "");
  browser_open_ = true;
  app_.invalidate_all();
}

// Folders and audio files (or presets) in `dir`, after an "Up a folder" entry (".."), selecting
// `select`. Looking for presets, a library bundle (.dsbundle) with one preset is listed as that
// preset ("Basic Piano.dsbundle/Basic Piano.dspreset", shown by the bundle's name); inside a bundle
// only its presets are listed, not its samples and images.
bool InstrumentView::list_browser_dir(const std::string& dir, const std::string& select) {
  Platform& p = app_.platform();
  std::vector<Platform::DirEntry> e;
  if (!p.list_dir(dir.c_str(), e)) return false;
  const bool presets = browse_ == Browse::Presets;
  const bool in_bundle = presets && is_bundle(dir);
  std::vector<Platform::DirEntry> kept;
  for (Platform::DirEntry& d : e) {
    if (!d.is_dir) {
      if (presets ? is_multisample_file(d.name) : is_audio_file(d.name)) kept.push_back(d);
      continue;
    }
    if (in_bundle) continue;
    if (presets && is_bundle(d.name)) {
      std::vector<Platform::DirEntry> inside;
      std::vector<std::string> found;
      if (p.list_dir((dir + d.name).c_str(), inside))
        for (const Platform::DirEntry& f : inside)
          if (!f.is_dir && is_multisample_file(f.name)) found.push_back(f.name);
      if (found.size() == 1) {
        kept.push_back(Platform::DirEntry{d.name + "/" + found[0], false});
        continue;
      }
    }
    kept.push_back(d);
  }
  // Folders first, then by name (a one-preset bundle is listed among the presets).
  std::stable_sort(kept.begin(), kept.end(), [](const Platform::DirEntry& a, const Platform::DirEntry& b) {
    return a.is_dir != b.is_dir ? a.is_dir : a.name < b.name;
  });
  if (dir != "/") kept.insert(kept.begin(), Platform::DirEntry{"..", true});
  dir_ = dir;
  entries_ = std::move(kept);
  bsel_ = btop_ = 0;
  for (size_t i = 0; i < entries_.size(); ++i)
    if (!select.empty() && entries_[i].name == select) bsel_ = int(i);
  // Otherwise the first real entry, so A doesn't go straight back up.
  if (select.empty() && entries_.size() > 1 && entries_[0].name == "..") bsel_ = 1;
  return true;
}

void InstrumentView::close_browser() {
  browser_open_ = false;
  app_.engine().preview(nullptr);
  app_.invalidate_all();
}

void InstrumentView::browser_go(const std::string& dir, const std::string& select) {
  if (!list_browser_dir(dir, select)) app_.message("Can't open that folder");
}

void InstrumentView::browser_up() {
  std::string parent = dir_.substr(0, dir_.size() - 1);
  std::string name = parent.substr(parent.find_last_of('/') + 1);
  parent = parent.substr(0, parent.find_last_of('/') + 1);
  browser_go(parent.empty() ? "/" : parent, name);
}

void InstrumentView::browser_choose(int entry, bool preview) {
  if (entry < 0 || entry >= int(entries_.size())) return;
  const Platform::DirEntry e = entries_[size_t(entry)];  // a copy: going to a folder replaces the list
  if (e.is_dir) {
    if (!preview) e.name == ".." ? browser_up() : browser_go(dir_ + e.name + "/", "");
    return;
  }
  std::string full = dir_ + e.name;
  // Store paths inside the data folder relative to it, so projects move with the folder.
  const std::string data = app_.platform().data_dir();
  std::string path = full.compare(0, data.size(), data) == 0 ? full.substr(data.size()) : full;
  std::string err;
  if (browse_ == Browse::Presets) {
    if (!preview) {
      on_pick_(path);  // loads, and says if it can't
      close_browser();
    }
  } else if (preview) {
    preview_ = app_.load_sample(path, &err);
    app_.engine().preview(preview_.get());
    if (!preview_) app_.message(err);
  } else if (!app_.load_sample(path, &err)) {
    app_.message(err);
  } else {
    on_pick_(path);
    close_browser();
  }
}

void InstrumentView::browser_input() {
  const InputTracker& in = app_.input();
  const int n = int(entries_.size());
  if (in.pressed(kX)) return close_browser();
  // Up/Down a file at a time (wrapping round), ten with R held (stopping at the ends); Left and
  // Right go to the top and the bottom.
  if (n > 0 && in.held(kR)) {
    if (in.pressed_or_repeat(kUp)) bsel_ = std::max(0, bsel_ - 10);
    if (in.pressed_or_repeat(kDown)) bsel_ = std::min(n - 1, bsel_ + 10);
  } else if (n > 0) {
    if (in.pressed_or_repeat(kUp)) bsel_ = (bsel_ + n - 1) % n;
    if (in.pressed_or_repeat(kDown)) bsel_ = (bsel_ + 1) % n;
  }
  if (n > 0 && in.pressed(kLeft)) bsel_ = 0;
  if (n > 0 && in.pressed(kRight)) bsel_ = n - 1;
  if (in.pressed(kB)) {
    if (dir_ == "/") return close_browser();
    browser_up();
  } else if (n > 0 && (in.pressed(kA) || in.pressed(kY))) {
    const bool was_open = browser_open_;
    browser_choose(bsel_, !in.pressed(kA));
    if (was_open && !browser_open_) return;
  }
  app_.invalidate(app_.content_rect());
}

// --- Input ------------------------------------------------------------------------------------

void InstrumentView::handle_input() {
  if (browser_open_) return browser_input();
  if (recorder_open_) return recorder_input();
#if TWILL_PLUGINS
  if (plugins_open_) return plugins_input();
#endif
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
#if TWILL_PLUGINS
  if (plugins_open_) return {{"Up/Down", "move"}, {"R+Up/Down", "by 10"}, {"A", "pick"}, {"B", "cancel"}};
#endif
  if (recorder_open_ && recorder_) {
    if (rec_warning_) return {{"A", "record anyway"}, {"B", "stop the song"}};
    switch (recorder_->state()) {
      case Recorder::State::Listening: return {{"A", "record now"}, {"B", "cancel"}};
      case Recorder::State::Recording: return {{"A", "stop"}, {"B", "cancel"}};
      case Recorder::State::Review: return {{"A", "keep"}, {"Y", "play"}, {"B", "again"}, {"X", "discard"}};
      default: return {{"A", "record"}, {"Up/Down", "choose"}, {"Left/Right", "change"}, {"B", "close"}};
    }
  }
  if (browser_open_)
    return {{"A", "open/pick"}, {"Y", "listen"}, {"B", "up a folder"}, {"X", "cancel"}, {"Left/Right", "top/end"},
            {"R+Up/Down", "by 10"}};
  return {{"Up/Down", "choose"}, {"Left/Right", "change"}, {"A", "browse"}, {"B", "reset"}, {"Y", "listen"},
          {"X", "track"}};
}

void InstrumentView::tick() {
  if (recorder_open_ && recorder_) {
    // The track it records into is gone or another is shown: put away.
    if (app_.track() != rec_track_ || rec_track_ >= int(app_.project().tracks.size())) {
      close_recorder();
      return;
    }
    const size_t had = recorder_->take_frames();
    recorder_->tick();
    // The take's peaks (the louder channel), 10 ms at a time, as it grows.
    const Recorder& t = *recorder_;
    const size_t frames = t.take_frames();
    const size_t bin = size_t(std::max(1, recorder_->rate() / 100));
    if (frames < had) rec_bins_.clear();
    if (rec_bins_.size() * bin > frames) rec_bins_.clear();
    for (size_t i = rec_bins_.empty() ? 0 : (rec_bins_.size() - 1) * bin; i < frames; i += bin) {
      float p = 0;
      for (size_t j = i; j < std::min(frames, i + bin); ++j) p = std::max(p, t.frame_peak(j));
      const size_t at = i / bin;
      if (at < rec_bins_.size()) rec_bins_[at] = p;
      else rec_bins_.push_back(p);
    }
    const Recorder::State st = recorder_->state();
    app_.set_sampling(st == Recorder::State::Recording);
    if (st != rec_drawn_) {
      rec_drawn_ = st;
      if (st == Recorder::State::Review) sel_ = 0, top_ = 0;
      if (st == Recorder::State::Listening || st == Recorder::State::Recording) sel_ = 5;  // Record now / Stop
      app_.invalidate(app_.content_rect());
    } else if (st != Recorder::State::Review) {
      app_.invalidate(picture_rect());  // the meter and the waveform
    }
    return;
  }
  // A multisample's zone map lights the zones sounding: redrawn when they change.
  const Track& t = app_.project().tracks[size_t(app_.track())];
  std::vector<int> now;
  if (!browser_open_ && t.instrument == InstrumentType::Sampler && t.sampler.mode == SamplerMode::Multi)
    app_.engine().sounding_zones(app_.track(), now);
  std::sort(now.begin(), now.end());
  now.erase(std::unique(now.begin(), now.end()), now.end());
  if (now != sounding_) {
    sounding_ = std::move(now);
    app_.invalidate(app_.content_rect());
  }
}

// --- Drawing ----------------------------------------------------------------------------------

void InstrumentView::draw(Canvas& c) {
  const Theme& th = app_.theme();
  const Rect content = app_.content_rect();
  if (browser_open_) return draw_browser(c, content);
#if TWILL_PLUGINS
  if (plugins_open_) return draw_plugins(c, content);
#endif
  const Track& t = app_.project().tracks[size_t(app_.track())];
  std::vector<Row> list = current_rows();
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
    const bool sel = i == sel_ && !row.dim;
    Rect rr{list_r.x + int(app_.u(4)), y, list_r.w - int(app_.u(8)), row_h};
    if (sel) c.fill(Rect{rr.x, rr.y + 1, rr.w, rr.h - 2}, th.chip_active);
    const int base = f.baseline_in(y, row_h - (row.fraction ? int(app_.u(3)) : 0));
    const std::string label = caps(row.label);
    const Color label_col = sel ? th.chip_active_text : row.dim ? th.divider : th.text_secondary;
    const float label_end = c.text(f, rr.x + app_.u(8), base, label.c_str(), label_col, sp);
    std::string v = fit_text(fb, row.value ? row.value() : "", rr.right() - app_.u(8) - (label_end + app_.u(10)));
    c.text(fb, rr.right() - app_.u(8) - fb.measure(v.c_str()), base, v.c_str(),
           sel ? th.chip_active_text : row.dim ? th.text_secondary : th.text);
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
  if (recorder_open_) return draw_recorder(c, pic);
#if TWILL_PLUGINS
  if (t.instrument == InstrumentType::Plugin) return draw_plugin(c, pic, t);
#endif
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
    return;
  }
  // The envelope while one of its settings is chosen; otherwise the instrument.
  const ParamId selected = sel_ >= 0 && sel_ < int(list.size()) ? list[size_t(sel_)].param : kParamNone;
  if (selected == kParamAttack || selected == kParamDecayTime || selected == kParamSustain || selected == kParamRelease) {
    draw_adsr(c, pic, t, selected);
  } else if (t.instrument == InstrumentType::Macro && (selected == kParamDecay || selected == kParamLpgColour)) {
    draw_lpg(c, pic, t, selected);
  } else if (t.instrument == InstrumentType::Macro) {
    draw_macro(c, pic, t);
  } else if (t.sampler.mode == SamplerMode::Kit) {
    draw_kit(c, pic, t);
  } else if (t.sampler.mode == SamplerMode::Multi) {
    draw_zones(c, pic, t);
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

namespace {

// Text wrapped into lines no wider than `w`.
std::vector<std::string> wrap(const Font& f, const std::string& text, float w) {
  std::vector<std::string> lines;
  std::string line, word;
  auto flush = [&] {
    if (word.empty()) return;
    const std::string tried = line.empty() ? word : line + " " + word;
    if (!line.empty() && f.measure(tried.c_str()) > w) {
      lines.push_back(line);
      line = word;
    } else {
      line = tried;
    }
    word.clear();
  };
  for (char ch : text) {
    if (ch == ' ') flush();
    else word += ch;
  }
  flush();
  if (!line.empty()) lines.push_back(line);
  return lines;
}

std::string seconds_text(float s) {
  char b[24];
  if (s < 1) std::snprintf(b, sizeof b, "%.0f ms", s * 1000);
  else std::snprintf(b, sizeof b, "%.1f s", s);
  return b;
}

std::string megabytes(size_t bytes) {
  char b[24];
  if (bytes < (10u << 20)) std::snprintf(b, sizeof b, "%.1f MB", bytes / 1048576.0);
  else std::snprintf(b, sizeof b, "%.0f MB", bytes / 1048576.0);
  return b;
}

}  // namespace

void InstrumentView::draw_zones(Canvas& c, const Rect& r, const Track& t) {
  const Theme& th = app_.theme();
  const Font& f = Font::get(FontFace::Regular, app_.u(kSmall + 1));
  const Font& fb = Font::get(FontFace::SemiBold, app_.u(kText));
  const Font& tiny = Font::get(FontFace::CondensedSemiBold, app_.u(kSmall));
  const int x = r.x + int(app_.u(12)), w = r.w - int(app_.u(24));
  auto centred = [&](const char* msg, int y) { c.text(f, r.x + (r.w - f.measure(msg)) / 2, y, msg, th.text_secondary); };
  if (t.sampler.preset.empty()) {
    centred("No preset yet", r.y + r.h / 2);
    return;
  }
  std::string err;
  const Multisample* m = app_.load_multisample(t.sampler.preset, &err);
  std::string name = file_name(t.sampler.preset);
  name = name.substr(0, name.find_last_of('.'));
  c.text(fb, x, r.y + int(app_.u(22)), name.c_str(), th.text);
  if (!m) {
    std::string lname;
    int done = 0, total = 0;
    if (app_.multisample_loading(t.sampler.preset)) {
      // Its progress, when it's the one loading now.
      const bool now = app_.loading_status(lname, done, total) && lname == name;
      const std::string msg = now && total ? "Loading " + std::to_string(done) + " of " + std::to_string(total) + " samples"
                                           : std::string("Waiting to load");
      centred(msg.c_str(), r.y + r.h / 2 - int(app_.u(8)));
      Rect bar{x + w / 6, r.y + r.h / 2 + int(app_.u(4)), w * 2 / 3, int(app_.u(6))};
      c.fill(bar, th.grid_step);
      if (now && total) c.fill(Rect{bar.x, bar.y, bar.w * done / total, bar.h}, app_.track_colour(app_.track()));
    } else {
      const std::vector<std::string> lines = wrap(f, err.empty() ? "Can't load this preset" : err, float(w));
      int y = r.y + int(app_.u(50));
      for (const std::string& l : lines) c.text(f, x, y, l.c_str(), th.text_secondary), y += int(app_.u(16));
    }
    return;
  }
  // What it is: zones, keys and memory.
  char info[96];
  std::snprintf(info, sizeof info, "%zu zones   %s", m->zones.size(), megabytes(app_.multisample_bytes(t.sampler.preset)).c_str());
  c.text(f, x, r.y + int(app_.u(40)), info, th.text_secondary);

  // The map: keys across, velocity up. Zones on top of each other (layers, round robins) show
  // darker. Below it a keyboard, with a dot on each key a sample is recorded at.
  int lo = std::max(0, m->lowest_key() - 2), hi = std::min(127, m->highest_key() + 2);
  while (hi - lo < 24) lo = std::max(0, lo - 1), hi = std::min(127, hi + 1);
  const int keys = hi - lo + 1;
  const int kb_h = int(app_.u(14)), label_h = int(app_.u(14)), foot = int(app_.u(18));
  Rect map{x, r.y + int(app_.u(52)), w, r.h - int(app_.u(52)) - kb_h - label_h - foot};
  if (map.h < int(app_.u(30))) return;
  auto kx = [&](int k) { return map.x + int(int64_t(k - lo) * map.w / keys); };
  auto vy = [&](int v) { return map.bottom() - int(int64_t(v) * map.h / 128); };
  c.fill(map, th.panel);
  for (int k = lo; k <= hi; ++k)
    if (k % 12 == 0) c.vline(kx(k), map.y, map.bottom(), th.grid_step);
  const Color ink = th.track_ink(int(t.colour)), col = app_.track_colour(app_.track());
  auto zone_rect = [&](const Zone& z) {
    const int a = std::max(lo, z.lo_key), b = std::min(hi, z.hi_key);
    if (b < a) return Rect{};
    Rect zr{kx(a), vy(z.hi_vel + 1), std::max(1, kx(b + 1) - kx(a)), 0};
    zr.h = std::max(1, vy(z.lo_vel) - zr.y);
    return zr;
  };
  for (const Zone& z : m->zones) {
    const Rect zr = zone_rect(z);
    if (zr.empty()) continue;
    c.fill(zr, col.with_alpha(70));
    c.stroke_rect(zr, 1, ink.with_alpha(160));
  }
  // The zones sounding now, solid, on top.
  for (int i : sounding_) {
    if (i < 0 || i >= int(m->zones.size())) continue;
    const Rect zr = zone_rect(m->zones[size_t(i)]);
    if (zr.empty()) continue;
    c.fill(zr, col);
    c.stroke_rect(zr, std::max(1, int(app_.u(1.5f))), th.text);
  }
  c.stroke_rect(map, 1, th.divider);
  // Keyboard.
  Rect kb{map.x, map.bottom() + int(app_.u(2)), map.w, kb_h};
  c.fill(kb, th.background);
  for (int k = lo; k <= hi; ++k) {
    const int n = k % 12;
    const bool black = n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    if (black) c.fill(Rect{kx(k), kb.y, std::max(1, kx(k + 1) - kx(k)), kb.h * 3 / 5}, th.text);
    else if (n == 0 || n == 5) c.vline(kx(k), kb.y, kb.bottom(), th.divider);
  }
  c.stroke_rect(kb, 1, th.divider);
  std::set<int> roots;
  for (const Zone& z : m->zones) roots.insert(z.root);
  const int dot = std::max(2, int(app_.u(3)));
  for (int k : roots)
    if (k >= lo && k <= hi)
      c.fill(Rect{(kx(k) + kx(k + 1)) / 2 - dot / 2, kb.bottom() - dot - int(app_.u(2)), dot, dot}, app_.track_colour(app_.track()));
  // Octave names under the Cs, as room allows.
  int last_right = -1000;
  for (int k = lo; k <= hi; ++k) {
    if (k % 12) continue;
    const std::string nm = note_name(k);
    const int tx = kx(k);
    if (tx < last_right + int(app_.u(4)) || tx + tiny.measure(nm.c_str()) > r.right()) continue;
    c.text(tiny, tx, kb.bottom() + int(app_.u(12)), nm.c_str(), th.text_secondary);
    last_right = tx + int(tiny.measure(nm.c_str()));
  }
  // The memory all samples take, of what the device allows.
  std::string mem = "All samples " + megabytes(app_.loaded_sample_bytes());
  if (app_.sample_budget()) mem += " of " + megabytes(app_.sample_budget());
  c.text(tiny, x, r.bottom() - int(app_.u(6)), caps(mem).c_str(), th.text_secondary, app_.u(kCapsSpacing));
}

void InstrumentView::draw_adsr(Canvas& c, const Rect& r, const Track& t, ParamId selected) {
  const Theme& th = app_.theme();
  const Font& label = Font::get(FontFace::CondensedSemiBold, app_.u(kSmall + 1));
  const Font& value = Font::get(FontFace::CondensedBold, app_.u(kText));
  const float sp = app_.u(kCapsSpacing);
  const int x = r.x + int(app_.u(14)), w = r.w - int(app_.u(28));
  c.text(label, x, r.y + int(app_.u(24)), "AMP ENVELOPE", th.text_secondary, sp);
  Rect g{x, r.y + int(app_.u(40)), w, r.h - int(app_.u(96))};
  if (g.h < int(app_.u(30))) return;
  const ToneSettings& tone = t.tone;
  const float A = env_seconds(tone.attack), D = env_seconds(tone.decay), R = env_seconds(tone.release), S = tone.sustain;
  // Each stage's width follows its time on a log scale (1 ms to 10 s), so short and long times
  // both show; the held part of the note is a fixed stretch.
  const float wa = 0.12f + tone.attack, wd = 0.12f + tone.decay, ws = 0.45f, wr = 0.12f + tone.release;
  const float total = wa + wd + ws + wr;
  const int xa = g.x, xd = xa + int(g.w * wa / total), xs = xd + int(g.w * wd / total), xr = xs + int(g.w * ws / total),
            xe = g.right();
  // The level across the graph: linear attack, exponential decay and release (as the engine does).
  auto level = [&](int px) -> float {
    if (px < xd) return float(px - xa) / std::max(1, xd - xa);
    if (px < xs) {
      const float f = float(px - xd) / std::max(1, xs - xd);
      return S + (1 - S) * std::exp(-4.6f * f);
    }
    if (px < xr) return S;
    const float f = float(px - xr) / std::max(1, xe - xr);
    return S * std::exp(-4.6f * f);
  };
  c.fill(g, th.panel);
  const Color col = app_.track_colour(app_.track()), fill = th.track_fill(int(t.colour));
  // The selected stage tinted behind the curve.
  int sa = -1, sb = -1;
  if (selected == kParamAttack) sa = xa, sb = xd;
  else if (selected == kParamDecayTime) sa = xd, sb = xs;
  else if (selected == kParamSustain) sa = xs, sb = xr;
  else if (selected == kParamRelease) sa = xr, sb = xe;
  const int pad = int(app_.u(6));
  auto py = [&](float lv) { return float(g.bottom() - pad) - lv * float(g.h - 2 * pad); };
  for (int px = g.x; px < g.right(); ++px)
    c.vline(px, int(py(level(px))), g.bottom() - pad, px >= sa && px < sb ? col.with_alpha(170) : fill);
  for (int px = g.x; px + 1 < g.right(); ++px) {
    c.line(float(px), py(level(px)), float(px + 1), py(level(px + 1)), th.text);
    c.line(float(px), py(level(px)) + 1, float(px + 1), py(level(px + 1)) + 1, th.text);
  }
  for (int sx : {xd, xs, xr}) c.vline(sx, g.y, g.bottom(), th.grid_step);
  c.stroke_rect(g, 1, th.divider);
  // Each stage's name and value under it; the selected one in ink.
  struct Stage {
    const char* name;
    std::string text;
    int a, b;
    ParamId id;
  };
  char sus[16];
  std::snprintf(sus, sizeof sus, "%.0f%%", S * 100);
  const Stage stages[] = {{"A", seconds_text(A), xa, xd, kParamAttack},
                          {"D", seconds_text(D), xd, xs, kParamDecayTime},
                          {"S", sus, xs, xr, kParamSustain},
                          {"R", seconds_text(R), xr, xe, kParamRelease}};
  const int ly = g.bottom() + int(app_.u(16));
  for (const Stage& s : stages) {
    const bool on = s.id == selected;
    const int cx = (s.a + s.b) / 2;
    c.text(label, cx - label.measure(s.name) / 2, ly, s.name, on ? th.text : th.text_secondary);
    const Font& vf = on ? value : label;
    c.text(vf, cx - vf.measure(s.text.c_str()) / 2, ly + int(app_.u(16)), s.text.c_str(), on ? th.text : th.text_secondary);
  }
}

void InstrumentView::draw_lpg(Canvas& c, const Rect& r, const Track& t, ParamId selected) {
  const Theme& th = app_.theme();
  const Font& label = Font::get(FontFace::CondensedSemiBold, app_.u(kSmall + 1));
  const Font& f = Font::get(FontFace::Regular, app_.u(kSmall + 1));
  const float sp = app_.u(kCapsSpacing);
  const int x = r.x + int(app_.u(14)), w = r.w - int(app_.u(28));
  c.text(label, x, r.y + int(app_.u(24)), "LOW-PASS GATE", th.text_secondary, sp);
  const int engine = t.macro.engine;
  // Engines that shape their own sound over time don't go through the gate.
  if ((engine >= 2 && engine <= 4) || engine >= 19) {
    int y = r.y + int(app_.u(56));
    for (const std::string& l :
         wrap(f, "This engine makes its own envelope, so the low-pass gate isn't used: each note fades as the engine shapes it. "
                 "Set Amp to ADSR to shape it yourself.", float(w)))
      c.text(f, x, y, l.c_str(), th.text_secondary), y += int(app_.u(16));
    return;
  }
  Rect g{x, r.y + int(app_.u(40)), w, r.h - int(app_.u(96))};
  if (g.h < int(app_.u(30))) return;
  const float decay = t.macro.decay, colour = t.macro.lpg_colour;
  // A struck note, block by block (12 samples at 48 kHz), the way the engine's gate responds:
  // its level, and how open its filter is.
  std::vector<float> lvl, bright;
  float seconds;
  if (engine == 7) {
    // Chiptune Arp: the app's own envelope, a 1 ms attack and a decay to -60 dB.
    seconds = 0.35f * std::pow(10.0f, 1.6f * decay);
    const float span = seconds * 1.1f;
    for (int i = 0; i < 400; ++i) {
      const float ts = span * i / 399;
      lvl.push_back(ts < 0.001f ? ts / 0.001f : std::pow(0.001f, (ts - 0.001f) / seconds));
    }
  } else {
    const float block = 12.0f / 48000.0f;
    const float short_decay = 200.0f * block * std::pow(2.0f, -96.0f * decay / 12.0f);
    const float tail = 20.0f * block * std::pow(2.0f, (-72.0f * decay + 12.0f * colour) / 12.0f) - short_decay;
    const float attack = 261.6f / 48000.0f * 12.0f * 2.0f;  // middle C
    float state = 0;
    bool up = true;
    std::vector<float> raw_l, raw_b;
    for (int i = 0; i < 48000 * 12 / 12; ++i) {  // at most 12 s
      if (up) {
        state += attack;
        if (state >= 1) state = 1, up = false;
      }
      const float in = up ? state : 0.0f;
      const float err = in - state, s2 = state * state, s4 = s2 * s2;
      state += (err > 0 ? 0.6f : short_decay + (1 - s4) * tail) * err;
      const float tl = (1 - state) * (1 - state);
      raw_l.push_back(state);
      // Brightness: the filter's cutoff, and the highs let through around it.
      const float freq = (0.003f + 0.3f * state * state * state * state + colour * 0.04f) / 0.343f;
      const float bleed = (tl + (1 - tl) * colour) * colour * colour;
      raw_b.push_back(std::min(1.0f, std::max(freq, bleed * state)));
      if (!up && state < 0.001f) break;
    }
    seconds = raw_l.size() * block;
    const size_t n = 400;
    for (size_t i = 0; i < n; ++i) {
      const size_t k = std::min(raw_l.size() - 1, i * raw_l.size() / n);
      lvl.push_back(raw_l[k]);
      bright.push_back(raw_b[k]);
    }
  }
  c.fill(g, th.panel);
  const int pad = int(app_.u(6));
  auto py = [&](float v) { return float(g.bottom() - pad) - v * float(g.h - 2 * pad); };
  auto at = [&](const std::vector<float>& v, int px) { return v[std::min(v.size() - 1, size_t(px - g.x) * v.size() / size_t(g.w))]; };
  const Color fill = th.track_fill(int(t.colour)), col = app_.track_colour(app_.track());
  for (int px = g.x; px < g.right(); ++px) c.vline(px, int(py(at(lvl, px))), g.bottom() - pad, fill);
  for (int px = g.x; px + 1 < g.right(); ++px) {
    c.line(float(px), py(at(lvl, px)), float(px + 1), py(at(lvl, px + 1)), th.text);
    c.line(float(px), py(at(lvl, px)) + 1, float(px + 1), py(at(lvl, px + 1)) + 1, th.text);
    if (!bright.empty()) {
      c.line(float(px), py(at(bright, px)), float(px + 1), py(at(bright, px + 1)), col);
      c.line(float(px), py(at(bright, px)) + 1, float(px + 1), py(at(bright, px + 1)) + 1, col);
    }
  }
  c.stroke_rect(g, 1, th.divider);
  // A key, and how long the note lasts.
  const int ly = g.bottom() + int(app_.u(16));
  const int sw = int(app_.u(12)), sh = std::max(2, int(app_.u(2)));
  int lx = x;
  c.fill(Rect{lx, ly - int(app_.u(4)), sw, sh}, th.text);
  lx = int(c.text(label, lx + sw + int(app_.u(4)), ly, "LEVEL", selected == kParamDecay ? th.text : th.text_secondary, sp)) + int(app_.u(12));
  if (!bright.empty()) {
    c.fill(Rect{lx, ly - int(app_.u(4)), sw, sh}, col);
    c.text(label, lx + sw + int(app_.u(4)), ly, "BRIGHTNESS", selected == kParamLpgColour ? th.text : th.text_secondary, sp);
  }
  const std::string len = seconds_text(seconds);
  c.text(label, g.right() - label.measure(len.c_str()), ly, len.c_str(), th.text_secondary);
  if (engine == 7 && selected == kParamLpgColour)
    c.text(f, x, ly + int(app_.u(18)), "Chiptune Arp doesn't use the colour.", th.text_secondary);
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
  if (empty) c.text(f, r.x + app_.u(16), top_y + int(entries_.size() + 1) * row_h, 
                         browse_ == Browse::Presets ? "No folders or presets here" : "No folders or audio files here",
                         th.text_secondary);
  for (int i = btop_; i < int(entries_.size()) && i < btop_ + visible; ++i) {
    const int y = top_y + (i - btop_) * row_h;
    Rect rr{r.x + int(app_.u(6)), y, r.w - int(app_.u(12)), row_h};
    const bool sel = i == bsel_;
    if (sel) c.fill(Rect{rr.x, rr.y + 1, rr.w, rr.h - 2}, th.chip_active);
    const auto& e = entries_[size_t(i)];
    const int base = f.baseline_in(y, row_h);
    // A library bundle by its name ("Basic Piano"), a folder of presets or the one it holds.
    std::string label = e.name == ".." ? std::string("Up a folder") : e.is_dir ? e.name + "/" : e.name;
    if (e.name != ".." && is_bundle(e.is_dir ? e.name : e.name.substr(0, e.name.find('/')))) {
      const std::string b = e.is_dir ? e.name : e.name.substr(0, e.name.find('/'));
      label = b.substr(0, b.size() - 9) + (e.is_dir ? "/" : "");
    }
    c.text(e.is_dir ? fb : f, rr.x + app_.u(10), base, label.c_str(),
           sel ? th.chip_active_text : e.is_dir ? th.text : th.text_secondary);
  }
}

#if TWILL_PLUGINS
// --- Plugins ----------------------------------------------------------------------------------

void InstrumentView::open_plugins() {
  PluginHost* host = app_.plugin_host();
  if (!host) return;
  plugin_list_ = host->list();
  if (plugin_list_.empty()) {
    app_.message("No plugins installed");
    return;
  }
  const PluginSettings& now = app_.project().tracks[size_t(app_.track())].plugin;
  psel_ = ptop_ = 0;
  for (size_t i = 0; i < plugin_list_.size(); ++i)
    if (plugin_list_[i].id == now.id) psel_ = int(i);
  plugins_open_ = true;
  app_.invalidate_all();
}

void InstrumentView::plugins_input() {
  const InputTracker& in = app_.input();
  const int n = int(plugin_list_.size());
  const int by = in.held(kR) ? 10 : 1;
  if (in.pressed(kB) || in.pressed(kX)) {
    plugins_open_ = false;
    app_.invalidate_all();
    return;
  }
  if (n > 0 && in.pressed_or_repeat(kUp)) psel_ = std::max(0, psel_ - by);
  if (n > 0 && in.pressed_or_repeat(kDown)) psel_ = std::min(n - 1, psel_ + by);
  if (n > 0 && in.pressed(kA)) {
    app_.choose_plugin(app_.track(), plugin_list_[size_t(psel_)]);
    plugins_open_ = false;
    sel_ = 0;
    app_.invalidate_all();
    return;
  }
  app_.invalidate(app_.content_rect());
}

void InstrumentView::draw_plugins(Canvas& c, const Rect& r) {
  const Theme& th = app_.theme();
  const Font& f = Font::get(FontFace::Regular, app_.u(kText));
  const Font& fb = Font::get(FontFace::SemiBold, app_.u(kText));
  const Font& small = Font::get(FontFace::Regular, app_.u(kSmall));
  c.fill(r, th.panel);
  c.fill(Rect{r.x, r.y, r.w, int(app_.u(24))}, th.header);
  const std::string title = "Plugins: " + std::to_string(plugin_list_.size()) + " installed";
  c.text(small, r.x + app_.u(12), r.y + int(app_.u(16)), title.c_str(), th.text_secondary);
  const int row_h = int(app_.u(kRowH));
  const int top_y = r.y + int(app_.u(28));
  const int visible = std::max(1, (r.bottom() - top_y) / row_h);
  if (psel_ < ptop_) ptop_ = psel_;
  if (psel_ >= ptop_ + visible) ptop_ = psel_ - visible + 1;
  for (int i = ptop_; i < int(plugin_list_.size()) && i < ptop_ + visible; ++i) {
    const int y = top_y + (i - ptop_) * row_h;
    Rect rr{r.x + int(app_.u(6)), y, r.w - int(app_.u(12)), row_h};
    const bool sel = i == psel_;
    if (sel) c.fill(Rect{rr.x, rr.y + 1, rr.w, rr.h - 2}, th.chip_active);
    const PluginInfo& p = plugin_list_[size_t(i)];
    const int base = f.baseline_in(y, row_h);
    const float vendor_w = f.measure(p.vendor.c_str());
    const std::string name = fit_text(fb, p.name, rr.w - app_.u(30) - vendor_w);
    c.text(fb, rr.x + app_.u(10), base, name.c_str(), sel ? th.chip_active_text : th.text);
    c.text(f, rr.right() - app_.u(10) - vendor_w, base, p.vendor.c_str(),
           sel ? th.chip_active_text : th.text_secondary);
  }
}

void InstrumentView::draw_plugin(Canvas& c, const Rect& r, const Track& t) {
  const Theme& th = app_.theme();
  const Font& big = Font::get(FontFace::CondensedBold, app_.u(26));
  const Font& sm = Font::get(FontFace::Regular, app_.u(kText));
  const int x = r.x + int(app_.u(16)), w = r.w - int(app_.u(32));
  c.text(big, x, r.y + int(app_.u(70)), "PLUGIN", th.track_ink(int(t.colour)));
  if (t.plugin.name.empty()) {
    c.text(sm, x, r.y + int(app_.u(96)), "None yet: A on Plugin to choose one", th.text_secondary);
    return;
  }
  const std::string name = fit_text(sm, t.plugin.name, float(w));
  c.text(sm, x, r.y + int(app_.u(96)), name.c_str(), th.text);
  const std::string why = app_.plugin_error(t);
  const std::string status = !why.empty() ? fit_text(sm, why, float(w)) : "Plays the notes; Y to hear it";
  c.text(sm, x, r.y + int(app_.u(116)), status.c_str(), th.text_secondary);
}
#endif

// --- The pointer ------------------------------------------------------------------------------
// Where things are, as draw() lays them out.

Rect InstrumentView::list_rect() const {
  const Rect content = app_.content_rect();
  return Rect{content.x, content.y, content.w * 52 / 100, content.h};
}

Rect InstrumentView::picture_rect() const {
  const Rect content = app_.content_rect(), l = list_rect();
  const int rule = std::max(1, int(app_.u(1)));
  return Rect{l.right() + rule, content.y, content.right() - l.right() - rule, content.h};
}

Rect InstrumentView::bar_in(const Rect& rr) const {
  const int bw = int(app_.u(64));
  return Rect{rr.right() - int(app_.u(8)) - bw, rr.y + rr.h - int(app_.u(5)), bw, std::max(2, int(app_.u(2)))};
}

int InstrumentView::row_at(int count, int x, int y) const {
  const Rect l = list_rect();
  const int row_h = int(app_.u(kRowH)), y0 = l.y + int(app_.u(4));
  if (!l.contains(x, y) || y < y0) return -1;
  const int i = top_ + (y - y0) / row_h;
  return i < count ? i : -1;
}

Rect InstrumentView::row_rect(int i) {
  const Rect l = list_rect();
  const int row_h = int(app_.u(kRowH)), visible = std::max(1, (l.h - int(app_.u(8))) / row_h);
  if (i < top_ || i >= top_ + visible || i >= int(current_rows().size())) return Rect{};
  return Rect{l.x + int(app_.u(4)), l.y + int(app_.u(4)) + (i - top_) * row_h, l.w - int(app_.u(8)), row_h};
}

Rect InstrumentView::value_bar_rect(int i) {
  const Rect rr = row_rect(i);
  return rr.empty() ? rr : bar_in(rr);
}

int InstrumentView::find_row(const char* label) {
  const std::vector<Row> list = current_rows();
  for (size_t i = 0; i < list.size(); ++i)
    if (list[i].label == label) return int(i);
  return -1;
}

Rect InstrumentView::entry_rect(int i) const {
  const Rect r = app_.content_rect();
  const int row_h = int(app_.u(kRowH)), top_y = r.y + int(app_.u(28));
  int top = btop_;
#if TWILL_PLUGINS
  if (plugins_open_) top = ptop_;
#endif
  if (i < top) return Rect{};
  return Rect{r.x + int(app_.u(6)), top_y + (i - top) * row_h, r.w - int(app_.u(12)), row_h}.intersect(r);
}

int InstrumentView::entry_at(int x, int y) const {
  const Rect r = app_.content_rect();
  const int row_h = int(app_.u(kRowH)), top_y = r.y + int(app_.u(28));
  if (!r.contains(x, y) || y < top_y) return -1;
  int top = btop_, n = int(entries_.size());
#if TWILL_PLUGINS
  if (plugins_open_) top = ptop_, n = int(plugin_list_.size());
#endif
  const int i = top + (y - top_y) / row_h;
  return i < n ? i : -1;
}

Rect InstrumentView::kit_pad_rect(int i) const {
  const Rect r = picture_rect();
  const int pad = int(app_.u(10)), gap = int(app_.u(6));
  const int cols = 4, rows = 4;
  const int cw = (r.w - 2 * pad - (cols - 1) * gap) / cols, ch = (r.h - 2 * pad - (rows - 1) * gap) / rows;
  return Rect{r.x + pad + (i % cols) * (cw + gap), r.y + pad + (i / cols) * (ch + gap), cw, ch};
}

void InstrumentView::step_selection(int dir) {
  // The way the panels go: a step through the list, stopping at the ends.
  const int n = int(current_rows().size());
  const int to = std::max(0, std::min(n - 1, sel_ + dir));
  if (to == sel_) return;
  sel_ = to;
  app_.invalidate(app_.content_rect());
}

void InstrumentView::set_from_bar(const Row& row, const Rect& bar, int x) {
  if (row.param == kParamNone) return;
  const ParamInfo* info = param_info(row.param);
  const float f = std::max(0.0f, std::min(1.0f, float(x - bar.x) / float(std::max(1, bar.w))));
  float v = info->min + f * (info->max - info->min);
  if (info->integer) v = std::round(v);
  app_.history().set_track_param(app_.track(), row.param, v);
  app_.invalidate(app_.content_rect());
}

void InstrumentView::pointer(const Gesture& g) {
  if (browser_open_) return pointer_browser(g);
#if TWILL_PLUGINS
  if (plugins_open_) return pointer_browser(g);
#endif
  pointer_list(g);
}

void InstrumentView::pointer_list(const Gesture& g) {
  const Rect content = app_.content_rect(), lr = list_rect();
  EditHistory& h = app_.history();
  std::vector<Row> list = current_rows();
  const int n = int(list.size());
  // On a setting's bar (a little round it, the row's height): set there.
  auto on_bar = [&](int i, int x) {
    if (i < 0 || i >= n || list[size_t(i)].param == kParamNone || !list[size_t(i)].fraction) return false;
    const Rect bar = bar_in(row_rect(i));
    const int reach = int(app_.u(g.touch() ? 8 : 4));
    return !bar.empty() && x >= bar.x - reach && x < bar.right() + reach;
  };
  auto end_group = [&] {
    if (pointer_group_) h.end_group();
    pointer_group_ = false;
  };
  auto begin_group = [&] {
    if (!pointer_group_) h.begin_group();
    pointer_group_ = true;
  };

  switch (g.kind) {
    case Gesture::Press: {
      press_was_selected_ = press_acted_ = false;
      slide_ = Slide::Off;
      if (!content.contains(g.x, g.y)) return;
      // The recorder's picture: one big button (A; in Keep?, Y: play). While the speakers card is
      // up, a press records anyway (a right-click: stops the song first, as B).
      if (recorder_open_ && recorder_) {
        if (rec_warning_) {
          rec_warning_ = false;
          if (g.button == 2 && app_.engine().playing()) app_.start_or_stop();
          recorder_go();
          app_.invalidate(content);
          return;
        }
        if (picture_rect().contains(g.x, g.y)) {
          if (g.button != 1) return;
          if (recorder_->state() == Recorder::State::Review) recorder_play();
          else recorder_go();
          app_.invalidate(content);
          return;
        }
      }
      // The picture: the sound (Y); a kit's pad: that pad.
      if (picture_rect().contains(g.x, g.y)) {
        if (g.button != 1) return;
        const Track& t = app_.project().tracks[size_t(app_.track())];
        if (t.instrument == InstrumentType::Sampler && t.sampler.mode == SamplerMode::Kit) {
          int pad = -1;
          for (int i = 0; i < kKitSlots; ++i)
            if (kit_pad_rect(i).contains(g.x, g.y)) pad = i;
          if (pad < 0) return;
          kit_slot_ = pad;
          app_.invalidate(content);
        }
        app_.audition(app_.track(), audition_note());
        return;
      }
      const int i = row_at(n, g.x, g.y);
      if (i < 0) return;
      // The second of a double click whose first did the row's job (opened the browser, say).
      if (g.taps == 2 && tap_acted_) {
        tap_acted_ = false;
        press_acted_ = true;
        return;
      }
      tap_acted_ = false;
      const Row& row = list[size_t(i)];
      const bool was_selected = i == sel_;
      if (!was_selected) {
        sel_ = i;
        app_.invalidate(content);
      }
      // A right-click, or a double click on a setting: its default (B).
      if ((g.button == 2 || (g.taps == 2 && !row.press)) && row.reset) {
        row.reset();
        press_acted_ = true;
        app_.invalidate(content);
        return;
      }
      if (g.button != 1) return;
      if (on_bar(i, g.x)) {  // set there, and a drag goes on setting it
        begin_group();
        slide_ = Slide::Value;
        slide_row_ = i;
        set_from_bar(row, bar_in(row_rect(i)), g.x);
        press_acted_ = true;
        return;
      }
      const bool act = row.press && (g.taps == 2 || (was_selected && !row.adjust && !g.touch()));
      if (act) {  // its job (A), at once; a finger waits for its tap, as it may be scrolling
        row.press();
        tap_acted_ = press_acted_ = true;
        app_.invalidate_all();
        return;
      }
      press_was_selected_ = was_selected;
      return;
    }
    case Gesture::Tap: {
      // Let go of on a row that was selected when pressed: its job (a finger's), or a step of its
      // value (Shift: the other way). A press on a bar is over.
      slide_ = Slide::Off;
      end_group();
      if (!press_was_selected_ || press_acted_) return;
      press_was_selected_ = false;
      const int i = row_at(n, g.x, g.y);
      if (i != sel_ || i < 0) return;
      const Row& row = list[size_t(i)];
      if (row.press) {
        row.press();
        tap_acted_ = true;
        app_.invalidate_all();
      } else if (row.adjust) {
        row.adjust(g.shift ? -1 : 1, false);
        app_.invalidate(content);
      }
      return;
    }
    case Gesture::DragStart:
      if (slide_ == Slide::Value) return;
      slide_ = lr.contains(g.start_x, g.start_y) ? Slide::None : Slide::Off;
      slide_row_ = row_at(n, g.start_x, g.start_y);
      slide_acc_ = 0;
      return;
    case Gesture::Drag:
    case Gesture::Pan: {
      if (g.kind == Gesture::Drag && slide_ == Slide::None) {
        // Settled by its first move: across a row that changes, its value; up and down, the list.
        const bool across = std::abs(g.x - g.start_x) > std::abs(g.y - g.start_y);
        if (across && slide_row_ >= 0 && slide_row_ < n && list[size_t(slide_row_)].adjust) {
          slide_ = Slide::Adjust;
          sel_ = slide_row_;
          begin_group();
          app_.invalidate(content);
        } else {
          slide_ = Slide::Scroll;
        }
      }
      if (g.kind == Gesture::Pan) {
        if (!lr.contains(g.x, g.y)) return;
      } else if (slide_ == Slide::Off) {
        return;
      }
      if (g.kind == Gesture::Drag && slide_ == Slide::Value) {
        if (slide_row_ >= 0 && slide_row_ < n) set_from_bar(list[size_t(slide_row_)], bar_in(row_rect(slide_row_)), g.x);
        return;
      }
      if (g.kind == Gesture::Drag && slide_ == Slide::Adjust) {
        const float step = app_.u(12);
        slide_acc_ += float(g.dx);
        while (std::abs(slide_acc_) >= step) {
          const int dir = slide_acc_ > 0 ? 1 : -1;
          slide_acc_ -= float(dir) * step;
          // The list may have changed with the last step (another engine): the row it started on.
          std::vector<Row> now = current_rows();
          if (sel_ >= int(now.size()) || !now[size_t(sel_)].adjust) return;
          now[size_t(sel_)].adjust(dir, false);
          app_.invalidate(content);
        }
        return;
      }
      // Through the list, the way the finger goes: dragged down, the rows above.
      slide_acc_ += float(g.dy);
      const float row_h = app_.u(kRowH);
      while (std::abs(slide_acc_) >= row_h) {
        const int dir = slide_acc_ > 0 ? -1 : 1;
        slide_acc_ += float(dir) * row_h;
        step_selection(dir);
      }
      return;
    }
    case Gesture::DragEnd:
    case Gesture::Cancel:
      slide_ = Slide::Off;
      end_group();
      return;
    case Gesture::Wheel: {
      if (!lr.contains(g.x, g.y)) return;
      wheel_acc_ += g.wheel_y;
      const int steps = int(wheel_acc_);
      wheel_acc_ -= float(steps);
      if (!steps) return;
      // Over a setting's bar: changes it (Shift: bigger steps); elsewhere through the list.
      const int i = row_at(n, g.x, g.y);
      if (on_bar(i, g.x) && list[size_t(i)].adjust) {
        sel_ = i;
        for (int k = 0; k < std::abs(steps); ++k) list[size_t(i)].adjust(steps > 0 ? 1 : -1, g.shift);
        app_.invalidate(content);
        return;
      }
      for (int k = 0; k < std::abs(steps); ++k) step_selection(steps > 0 ? -1 : 1);
      return;
    }
    case Gesture::LongPress:
    case Gesture::Pinch:
      return;
  }
}

void InstrumentView::pointer_browser(const Gesture& g) {
  const Rect content = app_.content_rect();
  // The file browser's list, or the plugin list.
  bool plugins = false;
  int* selp = &bsel_;
  int n = int(entries_.size());
#if TWILL_PLUGINS
  if (plugins_open_) plugins = true, selp = &psel_, n = int(plugin_list_.size());
#endif
  int& sel = *selp;
  auto close = [&] {
#if TWILL_PLUGINS
    if (plugins) {
      plugins_open_ = false;
      app_.invalidate_all();
      return;
    }
#endif
    close_browser();
  };
  auto activate = [&](int i) {
#if TWILL_PLUGINS
    if (plugins) {
      app_.choose_plugin(app_.track(), plugin_list_[size_t(i)]);
      plugins_open_ = false;
      sel_ = 0;
      app_.invalidate_all();
      return;
    }
#endif
    browser_choose(i, false);
    app_.invalidate_all();
  };
  auto move = [&](int dir) {
    if (n <= 0) return;
    sel = std::max(0, std::min(n - 1, sel + dir));
    app_.invalidate(content);
  };

  switch (g.kind) {
    case Gesture::Press: {
      press_was_selected_ = press_acted_ = false;
      if (!content.contains(g.x, g.y)) return;
      if (g.button == 2) return close();  // as X: put away
      if (g.button != 1) return;
      if (!plugins && g.y < content.y + int(app_.u(24))) {  // the folder's name: up a folder
        if (dir_ != "/") browser_up();
        app_.invalidate(content);
        return;
      }
      const int i = entry_at(g.x, g.y);
      if (i < 0) return;
      if (g.taps == 2 && tap_acted_) {  // its first click opened a folder: not for what's there now
        tap_acted_ = false;
        press_acted_ = true;
        return;
      }
      tap_acted_ = false;
      if (i != sel) {  // selected; an audio file is heard too (Y)
        sel = i;
        if (!plugins && browse_ == Browse::Samples && !entries_[size_t(i)].is_dir) browser_choose(i, true);
        app_.invalidate(content);
        return;
      }
      if (!g.touch() || g.taps == 2) {
        activate(i);
        tap_acted_ = press_acted_ = true;
        return;
      }
      press_was_selected_ = true;
      return;
    }
    case Gesture::Tap:
      if (!press_was_selected_ || press_acted_) return;
      press_was_selected_ = false;
      if (entry_at(g.x, g.y) == sel && sel >= 0 && sel < n) {
        activate(sel);
        tap_acted_ = true;
      }
      return;
    case Gesture::LongPress:  // a finger's right-click
      return close();
    case Gesture::DragStart:
      slide_ = content.contains(g.start_x, g.start_y) ? Slide::Scroll : Slide::Off;
      slide_acc_ = 0;
      return;
    case Gesture::Drag:
    case Gesture::Pan: {
      if (g.kind == Gesture::Drag && slide_ != Slide::Scroll) return;
      if (g.kind == Gesture::Pan && !content.contains(g.x, g.y)) return;
      slide_acc_ += float(g.dy);
      const float row_h = app_.u(kRowH);
      while (std::abs(slide_acc_) >= row_h) {
        const int dir = slide_acc_ > 0 ? -1 : 1;
        slide_acc_ += float(dir) * row_h;
        move(dir);
      }
      return;
    }
    case Gesture::DragEnd:
    case Gesture::Cancel:
      slide_ = Slide::Off;
      return;
    case Gesture::Wheel: {
      if (!content.contains(g.x, g.y)) return;
      wheel_acc_ += g.wheel_y;
      const int steps = int(wheel_acc_);
      wheel_acc_ -= float(steps);
      for (int k = 0; k < std::abs(steps); ++k) move(steps > 0 ? -1 : 1);
      return;
    }
    case Gesture::Pinch:
      return;
  }
}

// --- The sample recorder ----------------------------------------------------------------------

namespace {

// The level meter: segments from -48 to 0 dB, the loud end in warning colours, the peak marked.
void draw_level_meter(Canvas& c, const App& app, Rect m, float level_db, float peak_db, Color col, bool threshold) {
  const Theme& th = app.theme();
  const Font& small = Font::get(FontFace::CondensedSemiBold, app.u(kSmall));
  const int segs = 32, gap = std::max(1, int(app.u(1.5f)));
  const int sw = std::max(1, (m.w - (segs - 1) * gap) / segs);
  for (int i = 0; i < segs; ++i) {
    const float db = -48 + 48.0f * float(i + 1) / segs;
    const Color on_col = db > -6 ? th.cursor : db > -12 ? th.warning : col;
    c.fill(Rect{m.x + i * (sw + gap), m.y, sw, m.h}, db <= level_db ? on_col : th.grid_step);
  }
  auto x_of = [&](float db) { return m.x + int((std::max(-48.0f, std::min(0.0f, db)) + 48) / 48 * float(m.w - sw)); };
  if (peak_db > -48) c.fill(Rect{x_of(peak_db), m.y - int(app.u(2)), std::max(2, int(app.u(2))), m.h + int(app.u(4))}, th.text);
  if (threshold)
    c.fill(Rect{x_of(Recorder::kThresholdDb), m.y - int(app.u(5)), std::max(2, int(app.u(1.5f))), m.h + int(app.u(10))},
           th.cursor);
  for (int db : {-48, -24, -12, -6, 0}) {
    const int x = m.x + int((db + 48) / 48.0f * float(m.w - 1));
    const std::string t = std::to_string(db);
    const float w = small.measure(t.c_str());
    const float tx = db == 0 ? x - w : db == -48 ? x : x - w / 2;
    c.text(small, tx, m.bottom() + int(app.u(13)), t.c_str(), th.text_secondary);
  }
}

}  // namespace

std::string InstrumentView::chosen_input() const {
  const std::string want = app_.setting("rec.input");
  if (want.empty()) return want;
  for (const std::string& d : app_.platform().capture_devices())
    if (d == want) return d;
  return std::string();  // not there now (unplugged): the system's default
}

bool InstrumentView::input_is_mic() const {
  std::string n = chosen_input();
  if (n.empty()) {
    const std::vector<std::string> d = app_.platform().capture_devices();
    n = d.empty() ? std::string("mic") : d.front();
  }
  for (char& ch : n) ch = char(std::tolower(static_cast<unsigned char>(ch)));
  return n.find("mic") != std::string::npos;
}

std::string InstrumentView::take_name() const {
  std::string base;
  for (char ch : app_.project().tracks[size_t(rec_track_)].name)
    base += (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_' || ch == ' ') ? ch : '_';
  if (base.empty()) base = "Take";
  std::vector<Platform::DirEntry> have;
  app_.platform().list_dir("samples/Recorded", have);
  for (int n = 1;; ++n) {
    const std::string name = base + " rec " + std::to_string(n);
    bool taken = false;
    for (const auto& e : have) taken = taken || e.name == name + ".wav";
    if (!taken) return name;
  }
}

bool InstrumentView::reopen_input() {
  std::string err;
  const std::string want = chosen_input();
  if (!recorder_->open(want, err)) {
    // An input that can't be opened (on a handheld, one its sound system holds; or a default with
    // nothing to record from, as sndstation's built-in codec on the Brick Pro) mustn't keep the
    // recorder shut: the default, then each input listed, and the one that opens remembered.
    std::vector<std::string> others{std::string()};
    for (const std::string& d : app_.platform().capture_devices()) others.push_back(d);
    std::string opened;
    bool ok = false;
    std::string err2;
    for (const std::string& o : others) {
      if (o == want) continue;
      if (recorder_->open(o, err2)) {
        ok = true;
        opened = o;
        break;
      }
    }
    if (!ok) {
      app_.message(err);
      close_recorder();
      return false;
    }
    app_.set_setting("rec.input", opened);
    app_.message("Can't open " + (want.empty() ? std::string("the default input") : want) + ", so " +
                 (opened.empty() ? std::string("the default") : opened) + ": " + err);
  }
  if (recorder_->channel >= recorder_->input_channels()) recorder_->channel = 0;
  if (recorder_->input_channels() == 1) recorder_->channel = 0;
  rec_bins_.clear();
  return true;
}

void InstrumentView::open_recorder(int slot) {
  if (!recorder_) recorder_ = std::make_unique<Recorder>(app_.platform());
  rec_track_ = app_.track();
  rec_slot_ = slot;
  const std::string gain = app_.setting("rec.gain"), ch = app_.setting("rec.channel");
  recorder_->gain_db = gain.empty() ? 0 : std::max(Recorder::kMinGainDb, std::min(Recorder::kMaxGainDb, float(std::atof(gain.c_str()))));
  recorder_->channel = ch.empty() ? 0 : std::max(Recorder::kStereo, std::atoi(ch.c_str()));
  recorder_->normalize = app_.setting("rec.normalize") != "off";
  recorder_->monitor = app_.setting("rec.monitor") == "on";  // off at first: a mic would howl through speakers
  saved_sel_ = sel_;
  saved_top_ = top_;
  recorder_open_ = true;
  // The inputs first, while none is open: a handheld tries each (and leaves out those that won't
  // open), and the default may be one of them, which, open, would look busy.
  app_.platform().capture_devices();
  if (!reopen_input()) return;
  sel_ = top_ = 0;
  rec_warning_ = false;
  rec_drawn_ = recorder_->state();
  app_.invalidate_all();
}

void InstrumentView::close_recorder() {
  if (!recorder_open_) return;
  if (recorder_) recorder_->close();
  app_.engine().preview(nullptr);
  rec_preview_.reset();
  app_.set_sampling(false);
  recorder_open_ = rec_warning_ = false;
  rec_bins_.clear();
  sel_ = saved_sel_;
  top_ = saved_top_;
  app_.invalidate_all();
}

void InstrumentView::leave() { close_recorder(); }

std::vector<InstrumentView::Row> InstrumentView::recorder_rows() {
  App& app = app_;
  Recorder& r = *recorder_;
  std::vector<Row> out;
  auto action = [](const char* label, const char* key, std::function<void()> f) {
    Row row;
    row.label = label;
    row.value = [key] { return std::string(key); };
    row.press = std::move(f);
    return row;
  };
  const Recorder::State st = r.state();
  if (st == Recorder::State::Review) {
    Row norm;
    norm.label = "Normalize";
    norm.value = [&r] { return std::string(r.normalize ? "On" : "Off"); };
    norm.adjust = [&r, &app](int, bool) {
      r.normalize = !r.normalize;
      app.set_setting("rec.normalize", r.normalize ? "on" : "off");
    };
    out.push_back(norm);
    char b[64];
    const float rate = float(std::max(1, r.rate()));
    Row name;
    name.label = "Name";
    const std::string n = take_name();
    name.value = [n] { return n; };
    name.dim = true;
    out.push_back(name);
    Row len;
    len.label = "Length";
    std::snprintf(b, sizeof b, "%.2f s", float(r.keep_end() - r.keep_start()) / rate);
    const std::string ls = b;
    len.value = [ls] { return ls; };
    len.dim = true;
    out.push_back(len);
    Row cut;
    cut.label = "Trimmed";
    std::snprintf(b, sizeof b, "%.2f s before, %.2f s after", float(r.keep_start()) / rate,
                  float(r.take_frames() - r.keep_end()) / rate);
    const std::string cs = b;
    cut.value = [cs] { return cs; };
    cut.dim = true;
    out.push_back(cut);
    out.push_back(action("Keep", "press A", [this] { recorder_keep(); }));
    out.push_back(action("Play", "press Y", [this] { recorder_play(); }));
    out.push_back(action("Record again", "press B", [&r] { r.cancel(); }));
    out.push_back(action("Discard", "press X", [this] { close_recorder(); }));
    return out;
  }
  const bool busy = st != Recorder::State::Ready;
  const std::vector<std::string> inputs = app.platform().capture_devices();
  Row input;
  input.label = "Input";
  input.value = [this] {
    const std::string n = chosen_input();
    return n.empty() ? std::string("Default") : n;
  };
  if (!busy && !inputs.empty())
    input.adjust = [this, &app, inputs](int step, bool) {
      // The default, then each input by name.
      std::vector<std::string> options{""};
      options.insert(options.end(), inputs.begin(), inputs.end());
      const auto it = std::find(options.begin(), options.end(), chosen_input());
      const int at = it == options.end() ? 0 : int(it - options.begin()), n = int(options.size());
      // The next one that opens: one that won't is passed over (and said so).
      std::string skipped, err;
      for (int k = 1; k < n; ++k) {
        const std::string& o = options[size_t(((at + step * k) % n + n) % n)];
        if (recorder_->open(o, err)) {
          app.set_setting("rec.input", o);
          break;
        }
        if (skipped.empty()) skipped = "Can't open " + o + ": " + err;
      }
      reopen_input();
      if (!skipped.empty()) app.message(skipped);
    };
  input.dim = busy;
  out.push_back(input);
  Row ch;
  ch.label = "Channel";
  ch.value = [&r] {
    const int n = r.input_channels();
    if (n <= 1) return std::string("Mono");
    if (r.channel == Recorder::kStereo) return std::string(n == 2 ? "Stereo" : "Stereo (inputs 1 and 2)");
    if (r.channel < 0) return std::string("Mono mix");
    if (n == 2) return std::string(r.channel == 0 ? "Left" : "Right");
    return "Input " + std::to_string(r.channel + 1);
  };
  if (!busy && r.input_channels() > 1)
    ch.adjust = [&r, &app](int step, bool) {
      // Each channel, then the mono mix, then stereo.
      const int n = r.input_channels() + 2;
      int at = r.channel == Recorder::kStereo ? n - 1 : r.channel < 0 ? n - 2 : r.channel;
      at = (at + step + n) % n;
      r.channel = at == n - 1 ? Recorder::kStereo : at == n - 2 ? -1 : at;
      app.set_setting("rec.channel", std::to_string(r.channel));
    };
  ch.dim = busy;
  out.push_back(ch);
  Row gain;
  gain.label = "Gain";
  gain.value = [&r] {
    char b[16];
    std::snprintf(b, sizeof b, "%+.0f dB", double(r.gain_db));
    return std::string(b);
  };
  gain.fraction = [&r] { return (r.gain_db - Recorder::kMinGainDb) / (Recorder::kMaxGainDb - Recorder::kMinGainDb); };
  if (!busy) {
    gain.adjust = [&r, &app](int step, bool coarse) {
      r.gain_db = std::max(Recorder::kMinGainDb, std::min(Recorder::kMaxGainDb, r.gain_db + float(step * (coarse ? 6 : 1))));
      app.set_setting("rec.gain", std::to_string(int(std::lround(r.gain_db))));
    };
    gain.reset = [&r, &app] {
      r.gain_db = 0;
      app.set_setting("rec.gain", "0");
    };
  }
  gain.dim = busy;
  out.push_back(gain);
  Row mon;  // can be changed while recording too
  mon.label = "Monitor";
  mon.value = [&r] { return std::string(r.monitor ? "On" : "Off"); };
  mon.adjust = [&r, &app](int, bool) {
    r.monitor = !r.monitor;
    app.set_setting("rec.monitor", r.monitor ? "on" : "off");
  };
  out.push_back(mon);
  Row start;
  start.label = "Start";
  start.value = [&app] { return std::string(app.setting("rec.start") == "press" ? "On A" : "When sound starts"); };
  if (!busy)
    start.adjust = [&app](int, bool) { app.set_setting("rec.start", app.setting("rec.start") == "press" ? "sound" : "press"); };
  start.dim = busy;
  out.push_back(start);
  if (busy) {
    out.push_back(action(st == Recorder::State::Recording ? "Stop" : "Record now", "press A", [this] { recorder_go(); }));
    out.push_back(action("Cancel", "press B", [&r] { r.cancel(); }));
  } else {
    out.push_back(action("Record", "press A", [this] { recorder_go(); }));
    out.push_back(action("Close", "press B", [this] { close_recorder(); }));
  }
  return out;
}

void InstrumentView::recorder_go() {
  Recorder& r = *recorder_;
  switch (r.state()) {
    case Recorder::State::Ready:
      // Through the speakers the mic hears the song: said once.
      if (app_.engine().playing() && !rec_warned_ && input_is_mic()) {
        rec_warning_ = rec_warned_ = true;
        app_.invalidate(app_.content_rect());
        return;
      }
      rec_bins_.clear();
      app_.engine().preview(nullptr);
      r.start(app_.setting("rec.start") != "press");
      break;
    case Recorder::State::Listening: r.start(false); break;
    case Recorder::State::Recording: r.stop(); break;
    default: break;
  }
  app_.invalidate(app_.content_rect());
}

void InstrumentView::recorder_play() {
  const std::vector<uint8_t> wav = recorder_->wav();
  std::string err;
  rec_preview_ = decode_sample(wav.data(), wav.size(), "take", err);
  app_.engine().preview(rec_preview_.get());
}

void InstrumentView::recorder_keep() {
  const std::vector<uint8_t> wav = recorder_->wav();
  if (wav.size() <= 44) {
    app_.message("Nothing to keep");
    return;
  }
  Platform& p = app_.platform();
  const std::string name = take_name(), path = "samples/Recorded/" + name + ".wav";
  p.make_dir("samples/Recorded");
  if (!p.write_file(path.c_str(), wav)) {
    app_.message("Couldn't save " + name);
    return;
  }
  // Into the track, as if picked in the browser: one undo step, which brings back the old sample.
  const int track = rec_track_, slot = rec_slot_;
  Track copy = app_.project().tracks[size_t(track)];
  if (slot < 0) copy.sampler.sample = path;
  else copy.sampler.kit[slot].sample = path;
  app_.history().change_track(track, copy);
  close_recorder();
  app_.message("Kept " + name);
}

void InstrumentView::recorder_input() {
  const InputTracker& in = app_.input();
  Recorder& r = *recorder_;
  const Rect content = app_.content_rect();
  if (rec_warning_) {
    if (in.pressed(kA) || in.pressed(kB)) {
      rec_warning_ = false;
      if (in.pressed(kB) && app_.engine().playing()) app_.start_or_stop();
      recorder_go();
    }
    return;
  }
  switch (r.state()) {
    case Recorder::State::Listening:
    case Recorder::State::Recording:
      if (in.pressed(kA)) recorder_go();
      else if (in.pressed(kB)) r.cancel(), app_.invalidate(content);
      return;
    case Recorder::State::Ready:
    case Recorder::State::Review: {
      std::vector<Row> list = recorder_rows();
      const int n = int(list.size());
      auto usable = [&](int i) { return !list[size_t(i)].dim; };
      if (in.pressed_or_repeat(kUp | kDown)) {
        const int dir = in.pressed_or_repeat(kUp) ? -1 : 1;
        for (int k = 0; k < n; ++k) {
          sel_ = (sel_ + dir + n) % n;
          if (usable(sel_)) break;
        }
        app_.invalidate(content);
      }
      sel_ = std::max(0, std::min(sel_, n - 1));
      const Row& row = list[size_t(sel_)];
      const int step = in.pressed_or_repeat(kRight) ? 1 : in.pressed_or_repeat(kLeft) ? -1 : 0;
      if (step && row.adjust && !row.dim) {
        row.adjust(step, in.held(kR));
        app_.invalidate(content);
        return;  // the input may have been opened again
      }
      if (r.state() == Recorder::State::Ready) {
        if (in.pressed(kA)) row.press && !row.dim ? row.press() : recorder_go();
        else if (in.pressed(kB) || in.pressed(kX)) close_recorder();
        else if (in.pressed(kY)) app_.audition(app_.track(), audition_note());
      } else {
        if (in.pressed(kA)) row.press && !row.dim ? row.press() : recorder_keep();
        else if (in.pressed(kY)) recorder_play();
        else if (in.pressed(kB)) r.cancel(), app_.invalidate(content);
        else if (in.pressed(kX)) close_recorder();
      }
      if (in.pressed(~0u)) app_.invalidate(content);
      return;
    }
    case Recorder::State::Closed: close_recorder(); return;
  }
}

void InstrumentView::draw_recorder(Canvas& c, const Rect& pic) {
  const Theme& th = app_.theme();
  Recorder& r = *recorder_;
  const Recorder::State st = r.state();
  const Track& t = app_.project().tracks[size_t(rec_track_)];
  const Color col = app_.track_colour(rec_track_), ink = th.track_ink(int(t.colour));
  const Font& big = Font::get(FontFace::CondensedBold, app_.u(26));
  const Font& sm = Font::get(FontFace::Regular, app_.u(kText));

  // Heading and the line under it.
  std::string heading, sub;
  Color heading_col = ink;
  bool dot = false;
  char b[64];
  switch (st) {
    case Recorder::State::Listening:
      heading = "LISTENING";
      sub = "Recording starts at the first sound";
      heading_col = th.cursor;
      break;
    case Recorder::State::Recording: {
      const float secs = r.seconds();
      std::snprintf(b, sizeof b, "%d:%04.1f", int(secs) / 60, double(std::fmod(secs, 60.0f)));
      heading = b;
      sub = "Recording, up to 30 s";
      heading_col = th.text;
      dot = true;
      break;
    }
    case Recorder::State::Review:
      heading = "KEEP?";
      sub = r.normalize ? "Trimmed and normalized" : "Trimmed";
      break;
    default:
      heading = "READY";
      sub = rec_slot_ < 0 ? "Into " + t.name + ", saved in samples/Recorded"
                          : "Into " + t.name + ", slot " + std::to_string(rec_slot_ + 1) + ", saved in samples/Recorded";
      break;
  }
  float x = pic.x + app_.u(16);
  if (dot) {
    const int d = int(app_.u(14)), cy = pic.y + int(app_.u(32));
    for (int yy = -d / 2; yy <= d / 2; ++yy) {
      const int half = int(std::sqrt(float(d * d / 4 - yy * yy)));
      c.fill(Rect{int(x) + d / 2 - half, cy + yy, 2 * half + 1, 1}, th.cursor);
    }
    x += float(d) + app_.u(8);
  }
  c.text(big, x, pic.y + int(app_.u(42)), heading.c_str(), heading_col);
  c.text(sm, pic.x + app_.u(16), pic.y + int(app_.u(64)),
         fit_text(sm, sub, float(pic.w) - app_.u(32)).c_str(), th.text_secondary);

  // The meter (not in Keep?: nothing comes in that matters), then the waveform.
  const Rect meter{pic.x + int(app_.u(16)), pic.y + int(app_.u(84)), pic.w - int(app_.u(32)), int(app_.u(14))};
  Rect wave{pic.x + int(app_.u(16)), pic.y + int(app_.u(128)), pic.w - int(app_.u(32)),
            pic.bottom() - pic.y - int(app_.u(144))};
  if (st == Recorder::State::Review) wave = Rect{wave.x, meter.y, wave.w, wave.bottom() - meter.y};
  else draw_level_meter(c, app_, meter, r.level_db(), r.peak_db(), col, st == Recorder::State::Listening);
  c.fill(wave, th.row_white);
  c.stroke_rect(wave, std::max(1, int(app_.u(1))), th.divider);
  const int mid = wave.y + wave.h / 2;
  c.hline(wave.x, wave.right(), mid, th.grid_beat);
  const int nb = int(rec_bins_.size());
  if (nb == 0 || st == Recorder::State::Ready || st == Recorder::State::Listening) {
    const char* m = st == Recorder::State::Listening ? "Make a sound"
                    : app_.setting("rec.start") == "press" ? "Press A to record"
                                                            : "Press A, then make a sound";
    c.text(sm, wave.x + (wave.w - sm.measure(m)) / 2, mid - int(app_.u(8)), m, th.text_secondary);
  } else {
    // Recording: at least five seconds across, so the line moves; Keep?: the whole take.
    const int span = st == Recorder::State::Recording ? std::max(nb, 500) : nb;
    const float bin_rate = float(std::max(1, r.rate() / 100));
    const float cut0 = st == Recorder::State::Review ? float(r.keep_start()) / bin_rate : -1;
    const float cut1 = st == Recorder::State::Review ? float(r.keep_end()) / bin_rate : -1;
    const int cols = wave.w - 2;
    for (int xx = 0; xx < cols; ++xx) {
      const int b0 = int(int64_t(xx) * span / cols), b1 = std::max(b0 + 1, int(int64_t(xx + 1) * span / cols));
      if (b0 >= nb) break;
      float p = 0;
      for (int k = b0; k < std::min(b1, nb); ++k) p = std::max(p, rec_bins_[size_t(k)]);
      const int h = std::max(1, int(std::min(1.0f, p) * float(wave.h / 2 - int(app_.u(6)))));
      const bool cut = cut0 >= 0 && (float(b1) <= cut0 || float(b0) >= cut1);
      c.fill(Rect{wave.x + 1 + xx, mid - h, 1, 2 * h}, cut ? th.grid_bar : ink);
    }
    if (st == Recorder::State::Recording) {
      const int lx = wave.x + 1 + int(int64_t(nb) * cols / span);
      c.fill(Rect{std::min(lx, wave.right() - 2), wave.y + 1, std::max(2, int(app_.u(1.5f))), wave.h - 2}, th.cursor);
    }
    if (cut0 >= 0) {
      const Font& small = Font::get(FontFace::CondensedBold, app_.u(kSmall));
      const float sp = app_.u(kCapsSpacing);
      const int x0 = wave.x + 1 + int(cut0 * float(cols) / float(span)), x1 = wave.x + 1 + int(cut1 * float(cols) / float(span));
      c.fill(Rect{wave.x + 1, wave.y + 1, x0 - wave.x - 1, wave.h - 2}, th.text.with_alpha(14));
      c.fill(Rect{x1, wave.y + 1, wave.right() - 1 - x1, wave.h - 2}, th.text.with_alpha(14));
      for (int lx : {x0, x1}) {
        c.fill(Rect{lx, wave.y, std::max(1, int(app_.u(1.5f))), wave.h}, th.text);
        draw_marker(c, lx, wave.y + int(app_.u(6)), int(app_.u(5)), th.text);
      }
      c.text(small, x0 + app_.u(5), wave.bottom() - int(app_.u(6)), "START", th.text, sp);
      c.text(small, x1 - small.measure("END", sp) - app_.u(5), wave.bottom() - int(app_.u(6)), "END", th.text, sp);
    }
  }

  // The speakers card, over everything.
  if (rec_warning_) {
    const Rect content = app_.content_rect();
    const Font& tf = Font::get(FontFace::CondensedBold, app_.u(18));
    const int w = std::min(content.w - int(app_.u(40)), int(app_.u(420)));
    const int pad = int(app_.u(16)), line_h = int(app_.u(19));
    const std::vector<std::string> lines =
        wrap(sm, "If the audio input is a microphone, it will hear the speakers as well as you. With headphones it won't. "
                 "Press A to record anyway, or B to stop the song first.",
             float(w - 2 * pad));
    const int h = int(app_.u(44)) + int(lines.size()) * line_h + pad;
    const Rect card{content.x + (content.w - w) / 2, content.y + std::max(int(app_.u(8)), (content.h - h) / 2), w, h};
    c.fill(Rect{card.x + int(app_.u(4)), card.y + int(app_.u(4)), card.w, card.h}, th.text.with_alpha(40));
    c.fill(card, th.panel);
    c.stroke_rect(card, std::max(1, int(app_.u(1))), th.text);
    c.text(tf, card.x + pad, card.y + int(app_.u(26)), "THE SONG IS PLAYING", th.text);
    c.fill(Rect{card.x + pad, card.y + int(app_.u(34)), card.w - 2 * pad, std::max(1, int(app_.u(1)))}, th.text);
    int y = card.y + int(app_.u(44)) + int(app_.u(kText));
    for (const std::string& l : lines) {
      c.text(sm, card.x + pad, y, l.c_str(), th.text);
      y += line_h;
    }
  }
}

}  // namespace twill
