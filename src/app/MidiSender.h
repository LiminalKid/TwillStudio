// Sends the engine's MIDI out on time. The engine stamps each event with the sample it belongs to;
// this turns samples into times using the anchor each audio callback leaves (its first sample and
// when it ran), smoothed so callback jitter doesn't become MIDI jitter, then holds each event until
// the audio around it reaches the speaker (`latency_us` later).
#pragma once
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "engine/Engine.h"

namespace twill {

class MidiSender {
 public:
  MidiSender(Engine& engine, MidiPort* port) : engine_(engine), port_(port) {}
  ~MidiSender() { stop(); }
  void set_latency_us(int64_t us) { latency_us_.store(us); }

  // Either run a thread that sends as events fall due (devices) ...
  void start_thread();
  void stop();
  // ... or call this now and then, sending everything due by `now_us` (steady clock). Not both.
  void pump(int64_t now_us);

  static int64_t now_us();

 private:
  struct Pending {
    uint64_t sample;
    MidiMessage m;
  };
  Engine& engine_;
  MidiPort* port_;
  std::atomic<int64_t> latency_us_{0};
  // Steady-clock microseconds at sample 0, estimated from the anchors.
  bool have_offset_ = false;
  double offset_us_ = 0;
  std::vector<Pending> pending_;
  std::thread thread_;
  std::atomic<bool> quit_{false};
};

}  // namespace twill
