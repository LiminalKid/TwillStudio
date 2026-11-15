// What the audio thread plays: an immutable copy of the project, built on the UI thread after each
// edit and handed over by pointer (design §6, "Threads and edits"). Nothing in it changes after
// it's built, so the audio thread reads it without locks.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "model/Project.h"

namespace twill {

struct SampleData;
// Finds a loaded sample by path (on the UI thread, while building a snapshot). The samples must
// stay alive while any snapshot or voice may use them (the app's sample cache keeps them).
using SampleResolver = std::function<const SampleData*(const std::string& path)>;

// SamplerSettings without its strings, so the audio thread can copy it freely.
struct SnapSampler {
  SamplerMode mode = SamplerMode::Single;
  int root = 60;
  float fine = 0, start = 0, end = 1;
  LoopMode loop = LoopMode::Off;
  float loop_start = 0, loop_end = 1;
  bool reverse = false;
  int slices = 16;
  bool slice_transients = false;
  struct Slot {
    float tune = 0, level = 1, pan = 0;
    int choke = 0;
  } kit[kKitSlots];
  void set(ParamId id, float value);  // for parameter locks
};

// A lane's value at a moment, as the engine applies it.
struct LaneValue {
  ParamId id = kParamNone;
  float value = 0;
};

struct SnapClip {
  Tick length = 0;
  std::vector<Note> notes;  // sorted by start
  std::vector<Lane> lanes;
};

struct SnapPlacement {
  Tick start = 0, length = 0, offset = 0;
  const SnapClip* clip = nullptr;
};

struct SnapTrack {
  InstrumentType instrument = InstrumentType::Macro;
  int voices = 1;
  bool legato = false;  // one voice; touching or overlapping notes slur instead of retriggering
  MacroSettings macro;
  SnapSampler sampler;
  ToneSettings tone;
  const SampleData* sample = nullptr;              // the sampler's sample (single and slice modes)
  const SampleData* kit[kKitSlots] = {};           // kit mode
  std::vector<int> slice_points;  // slicing at transients: each slice's first frame, then the last's end
  float level = 0.8f, pan = 0;
  float send_chorus = 0, send_delay = 0, send_reverb = 0;
  Modulator mods[kModulators];
  int midi_channel = 0;
  int midi_cc[2] = {1, 74};
  bool audible = true;  // mute and solo, already resolved
  std::vector<SnapPlacement> placements;  // sorted by start
};

struct Snapshot {
  uint64_t revision = 0;  // the EditHistory revision it was built from
  float tempo = 120;
  bool midi_clock_out = false;  // 24 clocks a beat, with start and stop, while playing
  bool loop = false;
  Tick loop_start = 0, loop_end = 0;
  MixSettings mix;
  std::vector<SnapClip> clips;    // placements point into this; never resized after building
  std::vector<SnapTrack> tracks;
};

// Builds a snapshot of the first `max_tracks` tracks (the rest stay in the project, silent on this
// device; design §6, "Track count").
std::unique_ptr<Snapshot> build_snapshot(const Project& p, int max_tracks, uint64_t revision,
                                         const SampleResolver& samples = nullptr);

}  // namespace twill
