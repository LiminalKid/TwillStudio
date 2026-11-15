// Following an external MIDI clock (design §8): the tempo from the pulses' arrival times, and the
// transport from start, continue, stop and song position. The app turns its actions into engine
// calls and keeps the engine in step (App::follow_clock()).
#pragma once
#include <cstdint>
#include <vector>

#include "model/Project.h"
#include "platform/Midi.h"

namespace twill {

class ClockFollower {
 public:
  struct Action {
    enum Kind : uint8_t { None, Play, Stop, Locate } kind = None;
    bool from_start = false;  // Play: from the song's start (start) or where it is (continue)
    Tick position = 0;        // Locate
  };
  // A real-time or song-position message, stamped with time_us; anything else is ignored. Start
  // and continue wait for the next pulse, which is where the music starts.
  Action feed(const MidiMessage& m);
  void reset();

  bool has_tempo() const { return times_.size() >= 8; }
  double bpm() const { return bpm_; }
  bool running() const { return running_; }
  // Beats the master has played since it started, at `now_us`, from the pulses so far.
  double beats_at(int64_t now_us) const;

 private:
  void estimate();
  std::vector<int64_t> times_;  // recent pulses' times, oldest first
  double bpm_ = 120, interval_us_ = 20833;
  bool armed_ = false, from_start_ = false, running_ = false;
  int64_t pulses_ = 0, last_us_ = 0;
};

}  // namespace twill
