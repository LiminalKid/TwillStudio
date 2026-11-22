// The audio engine (design §6): a transport on a sample clock, a scheduler that walks each track's
// arrangement, a shared pool of voices (macro oscillator or sampler, each followed by filter, drive
// and amp envelope), and a simple mixer. Runs at 48 kHz.
//
// Threads: the UI thread calls submit(), the transport methods and collect_garbage(); the audio
// thread calls render(). They share only lock-free queues and atomics.
#pragma once
#include <atomic>
#include <cmath>
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
#if TWILL_PLUGINS
#include "platform/Plugins.h"
#endif

namespace twill {

class Engine {
 public:
  static constexpr int kSampleRate = PlaitsVoice::kSampleRate;
  static constexpr int kSubBlock = PlaitsVoice::kBlockSize;  // scheduling resolution, 0.25 ms
  // Audio leaves the engine this many frames after the sample clock it was made at (the master
  // limiter's look-ahead); MIDI out waits the same.
  static constexpr int kOutputDelay = Effects::kLimiterDelay;
#if TWILL_PLUGINS
  // Plugin tracks: their notes are gathered over this long, timed within it, and each plugin plays
  // the whole of it at once at its end, so it sounds this much after the other instruments (2 ms).
  static constexpr int kPluginBlock = 8 * kSubBlock;
#endif

  // For tests: told about every note the scheduler starts. Called on the audio thread.
  struct Observer {
    virtual ~Observer() = default;
    virtual void note_on(int track, uint8_t pitch, uint64_t sample, const MacroParams& params) = 0;
  };

  // max_synth_voices: how many of them may play the macro oscillator at once (it costs far more
  // than playing a sample); 0: as many as there are voices.
  explicit Engine(int max_voices, int max_synth_voices = 0);
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
  // The multisample zones sounding on `track` now (indexes in its Multisample's zones), as of the
  // last audio callback; for the sound view's zone map. Any thread.
  void sounding_zones(int track, std::vector<int>& out) const;
  // How many times a note took a voice that was still sounding because every voice was busy (a
  // note or tail cut off). It only goes up.
  uint32_t voices_stolen() const { return voices_stolen_.load(); }
  // Meters: the loudest sample since the last call, per track (after level and pan) and master.
  float take_track_peak(int track) { return track < kMaxTracks ? track_peak_[track].exchange(0) : 0; }
  float take_master_peak() { return master_peak_.exchange(0); }
  float compressor_reduction_db() const { return comp_gr_.load(); }  // the master compressor's, now
  // Audio load: the time a callback takes to make its block, as a share of the time the block lasts
  // (1 = only just in time). Averaged over about a second; the highest since the last take; the
  // highest ever; and how many callbacks took longer than their block lasts (each a dropout).
  float load_average() const { return load_avg_.load(std::memory_order_relaxed); }
  float take_load_peak() { return load_peak_.exchange(0); }
  float load_highest() const { return load_max_.load(std::memory_order_relaxed); }
  uint32_t late_blocks() const { return late_.load(std::memory_order_relaxed); }
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
  // Offline stems: no master EQ, compressor, level or limiter. Set before rendering starts.
  void set_master_bypass(bool on) { master_bypass_ = on; }
  // Gain before the master limiter, for a device whose output is quiet (App's Output boost); live
  // playback only: offline renders use an engine of their own, at 0 dB. Any thread.
  void set_output_boost_db(float db) { output_boost_.store(std::pow(10.0f, db / 20.0f)); }

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
    int xfade = 0;             // a multisample zone's loop crossfade, frames
    bool xfade_linear = false;
    bool done = false;
    float pan = 0;             // kit slot pan
    int choke = 0;
    bool live = false;         // held by a live note: released by live_note(.., 0), not a timer
    bool one_shot = false;     // a kit drum: plays to the end of its sample, whatever the note length
    float fade = 1;            // a kit slot's decay: the level now, and its fall per grid block
    float fade_per_block = 1;
    int zone = -1;             // a multisample's: its zone in the snapshot track's zones
  };

  void apply_command(const Command& c);
  // Starts the notes whose start falls in [t0, t1) ticks, at the current sub-block.
  void schedule(double t0, double t1);
  void start_note(int track, const Note& n, double tps);
  void start_voice(Voice& v, int track, const Note& n, int64_t gate, bool legato, int zone = -1);
  bool start_sampler(Voice& v, const SnapTrack& t, const Note& n, int zone);
  // A multisample's note: a voice for each zone it plays (layers, a round robin's turn), the track's
  // polyphony counted in notes. True if it's a multisample track (whether or not anything played).
  bool start_multi(int track, const SnapTrack& t, const Note& n, int64_t gate, bool live);
  void render_sampler(Voice& v, float* l, float* r, int n);
  void render_sampler_run(Voice& v, float* l, float* r, int n);  // no edges in reach
  void update_modulators(Voice& v, const SnapTrack& t);
  // LFOs that don't restart with each note: one phase per track, a grid block at a time.
  void update_track_lfos(bool playing);
  float lfo_hz_now(const Modulator& m) const;  // at the current tempo, when synced
  void update_lanes(double tick);
  // A voice for a new note. A track's polyphony (t.voices) counts its notes; in a kit it counts
  // each drum's own, so drums never cut each other off.
  Voice* allocate(int track, const SnapTrack& t, uint8_t pitch);
  // A free voice, else the oldest released, else the oldest of all. For a macro note with the
  // synth voices all in use, one of those instead.
  Voice* any_voice(bool synth);
  int max_synth_voices_ = 0;
  // Round robins: each preset group's turn, and the zone a random one played last, per track.
  static constexpr int kSeqGroups = 32;
  int seq_turn_[kMaxTracks][kSeqGroups] = {};
  int seq_last_[kMaxTracks][kSeqGroups] = {};
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
  // Per voice, what sounding_zones() reads: track << 20 | zone index, or -1.
  std::unique_ptr<std::atomic<int32_t>[]> voice_zones_;
  std::atomic<uint32_t> voices_stolen_{0};
  std::atomic<float> track_peak_[kMaxTracks] = {};
  std::atomic<float> master_peak_{0};
  std::atomic<float> comp_gr_{0};
  std::atomic<float> load_avg_{0}, load_peak_{0}, load_max_{0};
  std::atomic<uint32_t> late_{0};
  void render_block(float* out, int frames);
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
  bool master_bypass_ = false;
  std::atomic<float> output_boost_{1.0f};
  const SampleData* preview_ = nullptr;
  int preview_pos_ = 0;
  // Automation: each track's lane values at the current block (none while stopped).
  // MIDI Out notes waiting for their note off.
  struct PendingOff {
    uint8_t status = 0, pitch = 0;
    int64_t samples = 0;
  };
  PendingOff pending_offs_[64] = {};
#if TWILL_PLUGINS
  // Plugin tracks (audio thread): each one's notes for the block being gathered, and what it played
  // for the last, which sounds over this one. A plugin moved to another track keeps all of it.
  struct PluginTrack {
    PluginInstance* inst = nullptr;
    PluginEvent events[256];
    int count = 0;
    float out[2][kPluginBlock] = {};
    uint8_t held[128] = {};  // notes on, not yet off (a count: a note can be struck again while held)
  };
  PluginTrack plugin_[kMaxTracks];
  struct PluginOff {
    int track = -1;
    uint8_t pitch = 0;
    int64_t samples = 0;
  };
  PluginOff plugin_offs_[256] = {};
  void plugin_event(int track, uint8_t pitch, uint8_t velocity, uint64_t sample);
  void plugin_note(int track, uint8_t pitch, uint8_t velocity, int64_t gate);  // gate 0: held (live)
  void plugins_all_off();
  void sync_plugins();  // after a new snapshot: each track's instance, its state following it
  void run_plugins();   // at a plugin block's start: the last one played
#endif
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
