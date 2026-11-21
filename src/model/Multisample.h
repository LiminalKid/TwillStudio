// Multisamples (design §6, "Multisamples"): an instrument made of zones, each a sample played over
// a range of keys and velocities, read from a sample library's preset file. One model for every
// format; the readers fill it in.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "model/Project.h"

namespace twill {

enum class SeqMode : uint8_t {
  Always,      // plays every time
  RoundRobin,  // plays when its position comes up, in turn
  Random,      // one of its group's, at random, never the same twice running
  TrueRandom,  // one of its group's, at random
};

struct Zone {
  std::string sample;  // the file, as the platform reads it (absolute, or relative to the data folder)
  int lo_key = 0, hi_key = 127, lo_vel = 0, hi_vel = 127;
  int root = 60;
  float tune = 0;   // semitones
  float gain = 1;   // linear
  float pan = 0;    // -1..1
  // In the file's own frames; -1: the whole file.
  int start = 0, end = -1;
  bool loop = false;
  bool loop_if_marked = false;  // loops when the file has loop markers (SFZ's default), though `loop` is off
  int loop_start = -1, loop_end = -1;  // -1: the sample's own markers, else its whole length
  // A crossfade into the loop's end from just before its start, smoothing a loop the samples
  // weren't cut for: in the file's frames (DecentSampler's loopCrossfade), or in seconds (SFZ's
  // loop_crossfade); 0 for none. Equal power unless `crossfade_linear`.
  int loop_crossfade = 0;
  float loop_crossfade_s = 0;
  bool crossfade_linear = false;
  bool one_shot = false;  // plays to its end, whatever the note's length
  // Key tracking: 1 is a semitone a key, 0 the same pitch on every key. Tuning is added to the
  // key before tracking (DecentSampler's way) unless `tune_after_keytrack` (SFZ's).
  float keytrack = 1;
  bool tune_after_keytrack = false;
  float veltrack = 1;  // how much velocity sets the level: 1 all of it, 0 none
  // One random number a note, 0-1: the zone plays when it falls in [lo_rand, hi_rand) (SFZ).
  float lo_rand = 0, hi_rand = 1;
  SeqMode seq = SeqMode::Always;
  int seq_group = 0;     // which counter or choice the zone is in (one per preset group)
  int seq_length = 0;    // round robin: how many turns (0: found from the group's positions)
  int seq_position = 1;  // round robin: its turn, from 1
  bool operator==(const Zone& o) const;
};

struct Envelope {
  float attack = 0, decay = 0, sustain = 1, release = 0;  // seconds, and the sustain level 0-1
  bool operator==(const Envelope& o) const {
    return attack == o.attack && decay == o.decay && sustain == o.sustain && release == o.release;
  }
};

struct Multisample {
  std::string name;
  std::vector<Zone> zones;
  bool has_envelope = false;  // the preset sets an amp envelope
  Envelope envelope;          // the first group's, when it does
  bool mixed_envelopes = false;  // its groups set different ones (the first is used)
  std::vector<std::string> unused;  // what the preset has that isn't played: "effects", "release triggers"...
  int lowest_key() const;
  int highest_key() const;
};

// The preset formats the app reads, by file name: .dspreset and .sfz.
bool is_multisample_file(const std::string& name);

// Reads a preset of either format; `includes` reads a file an SFZ includes (a path relative to
// the preset's folder), false if it can't.
using IncludeReader = std::function<bool(const std::string& path, std::string& text)>;
bool read_multisample(const std::string& path, const std::string& text, Multisample& out, std::string& error,
                      const IncludeReader& includes = nullptr);

// Reads a DecentSampler preset (.dspreset), written from the format's public documentation. Sample
// paths are made relative to `folder`, the preset's own folder (ending with '/'). False, with why in
// `error`, when the text isn't a preset with any sample zones.
bool read_dspreset(const std::string& text, const std::string& folder, Multisample& out, std::string& error);

// Reads an SFZ file, written from the format's public documentation (sfzformat.com): regions with
// what <global>, <master> and <group> headers set above them, #define and #include, default_path.
bool read_sfz(const std::string& text, const std::string& folder, Multisample& out, std::string& error,
              const IncludeReader& includes = nullptr);

}  // namespace twill
