// The audio engine (design §6): a transport on a sample clock, a scheduler that walks each track's
// arrangement, a shared pool of voices (macro oscillator or sampler, each followed by filter, drive
// and amp envelope), and a simple mixer. Runs at 48 kHz.
//
// Threads: the UI thread calls submit(), the transport methods and collect_garbage(); the audio
// thread calls render(). They share only lock-free queues and atomics.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "audio/SpscQueue.h"
#include "engine/Effects.h"
#include "engine/PlaitsVoice.h"
#include "engine/Sample.h"
#include "engine/Snapshot.h"
#include "engine/VoiceDsp.h"
#include "platform/Midi.h"

namespace twill {

class Engine {
 public:
  static constexpr int kSampleRate = PlaitsVoice::kSampleRate;
  static constexpr int kSubBlock = PlaitsVoice::kBlockSize;  // scheduling resolution, 0.25 ms
  // Audio leaves the engine this many frames after the sample clock it was made at (the master
  // limiter's look-ahead); MIDI out waits the same.
  static constexpr int kOutputDelay = Effects::kLimiterDelay;

  // For tests: told about every note the scheduler starts. Called on the audio thread.
  struct Observer {
    virtual ~Observer() = default;
    virtual void note_on(int track, uint8_t pitch, uint64_t sample, const MacroParams& params) = 0;
  };

  explicit Engine(int max_voices);
  ~Engine();

  // UI thread.
  void submit(std::unique_ptr<Snapshot> snapshot);  // the audio thread switches to it at once
  void collect_garbage();                            // frees snapshots the audio thread let go of
  void play();
  void stop();                                       // stops and silences
  void locate(Tick position);
  // Plays at this tempo instead of the project's (following an external MIDI clock); 0 goes back.
  void set_tempo_override(double bpm);
  // Ticks played since the last play(), however the song position jumped (loops, locates): for
  // keeping in step with an external clock.
  double elapsed_ticks() const { return elapsed_ticks_.load(std::memory_order_relaxed); }
  bool playing() const { return playing_.load(); }
  Tick position() const { return position_.load(); }
  // The revision of the snapshot playing. Once it reaches a snapshot's, no voice uses a sample that
  // snapshot doesn't (voices on samples a new snapshot drops are stopped when it arrives).
  uint64_t playing_revision() const { return revision_.load(); }
  // Audio callbacks so far. Once it has gone up by two, every command sent before has been applied.
  uint64_t callbacks() const { return callbacks_.load(); }
  int active_voices() const { return active_voices_.load(); }
  // How many times a note took a voice that was still sounding because every voice was busy (a
  // note or tail cut off). It only goes up.
  uint32_t voices_stolen() const { return voices_stolen_.load(); }
  // Meters: the loudest sample since the last call, per track (after level and pan) and master.
  float take_track_peak(int track) { return track < kMaxTracks ? track_peak_[track].exchange(0) : 0; }
  float take_master_peak() { return master_peak_.exchange(0); }
  float compressor_reduction_db() const { return comp_gr_.load(); }  // the master compressor's, now
  // Plays a note on a track now, whether or not the transport is running.
  void audition(int track, uint8_t pitch, float seconds);
  // Plays a sample as it is (the sample browser). The caller keeps it alive until it has finished
  // or preview(nullptr) has been called and a callback has passed.
  void preview(const SampleData* sample);
  // A note played live (a MIDI keyboard): velocity 0 releases it. On MIDI Out tracks it goes
  // straight out.
  void live_note(int track, uint8_t pitch, uint8_t velocity);
  // MIDI the engine wants sent (MIDI Out tracks), oldest first. UI thread.
  // MIDI out, stamped with the sample it belongs to (the engine's clock), oldest first. Each callback
  // also adds an anchor: the sample it started at and the time it ran (steady clock, microseconds),
  // which is how a sender turns samples into times. Read by one thread only.
  struct MidiEvent {
    enum Kind : uint8_t { Message, Now, Anchor } kind = Message;  // Now: a live note, send at once
    uint64_t sample = 0;
    int64_t wall_us = 0;  // anchors only
    MidiMessage m;
  };
  bool pop_midi_out(MidiEvent& e) { return midi_out_.pop(e); }
  void set_observer(Observer* o) { observer_ = o; }  // set before rendering starts

  // Audio thread. Interleaved stereo.
  void render(float* out, int frames);

 private:
  struct Command {
    enum Kind : uint8_t { Play, Stop, Locate, Audition, Preview, LiveOn, LiveOff, Tempo } kind;
    int track;
    uint8_t pitch;
    double value;
    const SampleData* sample;
  };
  struct Voice {
    int track = -1;            // -1: free
    InstrumentType instrument = InstrumentType::Macro;
    Note note;                 // the note playing (pitch, velocity, parameter locks)
    float gain = 1;
    int64_t gate = 0;          // samples until note off; 0 once released
    uint64_t started = 0;      // sample clock at note on, for stealing the oldest
    uint64_t released = 0;     // sample clock at note off, to tell a note that ends where the next begins
    Adsr env;
    ToneStage tone;
    // Modulators. A legato note carries them on from the note before.
    uint64_t mod_started = 0;  // sample clock the modulator envelopes run from
    float mod_phase[kModulators] = {};
    float mod_random[kModulators] = {};
    float mod_out[kModulators] = {};
    // Macro oscillator.
    PlaitsVoice synth;
    MacroParams params;
    float glide_note = 60;     // the pitch, sliding towards pitch_target when gliding
    float pitch_target = 60;
    int quiet = 0;             // consecutive quiet grid blocks after release
    float block_peak = 0;      // loudest sample so far in the current grid block
    float last_peak = 0;       // the loudest sample in the last whole grid block
    // Sampler.
    const SampleData* sample = nullptr;
    double pos = 0, inc = 1, base_inc = 1;
    int dir = 1;
    int begin = 0, end = 0, loop_a = 0, loop_b = 0;
    LoopMode loop = LoopMode::Off;
    bool done = false;
    float pan = 0;             // kit slot pan
    int choke = 0;
    bool live = false;         // held by a live note: released by live_note(.., 0), not a timer
  };

  void apply_command(const Command& c);
  // Starts the notes whose start falls in [t0, t1) ticks, at the current sub-block.
  void schedule(double t0, double t1);
  void start_note(int track, const Note& n, double tps);
  void start_voice(Voice& v, int track, const Note& n, int64_t gate, bool legato);
  bool start_sampler(Voice& v, const SnapTrack& t, const Note& n);
  void render_sampler(Voice& v, float* l, float* r, int n);
  void update_modulators(Voice& v, const SnapTrack& t);
  // LFOs that don't restart with each note: one phase per track, a grid block at a time.
  void update_track_lfos(bool playing);
  float lfo_hz_now(const Modulator& m) const;  // at the current tempo, when synced
  void update_lanes(double tick);
  // A voice for a new note. A track's polyphony (t.voices) counts its notes; in a kit it counts
  // each drum's own, so drums never cut each other off.
  Voice* allocate(int track, const SnapTrack& t, uint8_t pitch);
  void release(Voice& v);
  void release_all();
  void midi_note(const SnapTrack& t, uint8_t pitch, uint8_t velocity, int64_t gate);
  void midi_out(const MidiMessage& m, uint64_t sample) { midi_out_.push(MidiEvent{MidiEvent::Message, sample, 0, m}); }
  void flush_midi_notes();
  // MIDI Out controllers: lanes send a value when it changes; a note's lock holds the value it
  // sets for as long as the note lasts.
  void send_cc(int track, int which, int value);
  void update_midi_cc();
  void reset_midi_cc();

  // Shared.
  SpscQueue<Snapshot*, 16> incoming_;
  SpscQueue<Snapshot*, 16> outgoing_;
  SpscQueue<Command, 64> commands_;
  SpscQueue<MidiEvent, 1024> midi_out_;
  std::atomic<bool> playing_{false};
  std::atomic<Tick> position_{0};
  std::atomic<uint64_t> revision_{0};
  std::atomic<uint64_t> callbacks_{0};
  std::atomic<double> elapsed_ticks_{0};
  std::atomic<int> active_voices_{0};
  std::atomic<uint32_t> voices_stolen_{0};
  std::atomic<float> track_peak_[kMaxTracks] = {};
  std::atomic<float> master_peak_{0};
  std::atomic<float> comp_gr_{0};
  Observer* observer_ = nullptr;

  // Audio thread.
  Snapshot* snap_ = nullptr;
  float track_lfo_phase_[kMaxTracks][kModulators] = {};
  float track_lfo_random_[kMaxTracks][kModulators] = {};
  Snapshot* pending_free_[8] = {};  // snapshots waiting for room in outgoing_
  std::vector<std::unique_ptr<Voice>> voices_;
  double tick_ = 0;            // transport position, in ticks
  double anchor_tick_ = 0;     // tick_ = anchor_tick_ + (clock_ - anchor_clock_) * ticks per sample
  uint64_t anchor_clock_ = 0;
  uint64_t clock_ = 0;         // samples rendered since start
  uint32_t rng_ = 0x2545F491;  // for note probability; fixed seed so renders repeat exactly
  std::unique_ptr<Effects> effects_;
  const SampleData* preview_ = nullptr;
  int preview_pos_ = 0;
  // Automation: each track's lane values at the current block (none while stopped).
  // MIDI Out notes waiting for their note off.
  struct PendingOff {
    uint8_t status = 0, pitch = 0;
    int64_t samples = 0;
  };
  PendingOff pending_offs_[64] = {};
  double next_midi_clock_ = 0;  // the sample of the next MIDI clock while playing
  int16_t cc_sent_[kMaxTracks][2] = {};       // the last value sent; -1: none since play
  uint64_t cc_held_until_[kMaxTracks][2] = {};  // a note's lock holds the value until this sample
  LaneValue lane_values_[kMaxTracks][kMaxLanesPerClip] = {};
  MixSettings mix_;          // the project's master and effects, with automation on top
  double tempo_override_ = 0;
  double tempo() const { return tempo_override_ > 0 ? tempo_override_ : double(snap_->tempo); }
  double elapsed_ = 0;       // elapsed_ticks_, on the audio thread
  int lane_counts_[kMaxTracks] = {};
};

}  // namespace twill
