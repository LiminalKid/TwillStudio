# Twill Studio: design

*Twill Studio (called Fluorescent, a placeholder, until 2026-11-14). A portable groovebox with
Plaits and sample instruments, edited in a piano roll. Draft 3, 2026-11-11.*

## 1. What it is

Something with the depth of the Dirtywave M8, but edited in a clean piano roll instead of tracker
screens:

- **Tracks**, as many as the platform can run (§6, "Track count"), each playing one instrument: **Plaits** (Mutable Instruments' macro-oscillator, all 24
  engines; a superset of the Braids engine behind M8's Macrosynth), **Sampler** (pitched, looped or
  sliced) or **MIDI Out**. Later perhaps an FM synth and a wavetable synth of our own.
- **Instruments are deep, as on the M8:** a filter and drive after the sound source, **modulators**
  (envelopes and LFOs that can drive any parameter), and send levels to shared effects.
- **Notes carry expression:** each note has velocity, length, probability, glide, and **parameter
  locks**: its own values for any instrument parameter, Elektron-style. That's the piano-roll
  equivalent of M8's per-step FX commands.
- **Automation lanes** under the piano roll draw parameter changes over time, where the M8 would use
  FX columns and tables.
- **Clips and an arrangement:** each track's music lives in clips, placed on a timeline. A clip can be
  reused, so editing one copy edits every placement, like M8 chains and phrases.
- **Mixer:** per-track level, pan, mute/solo, and sends to **chorus, delay and reverb**, then master EQ
  and a limiter.
- **Portable** across handhelds (TrimUI Brick Pro first), desktop and, later, other targets, the way
  LittleGPTracker (LGPT) is.

### Non-goals (for now)

- No tracker views. Everything is edited as notes, curves and clips.
- No plugin hosting and no audio tracks (recording long takes to a timeline).
- No targets without floating-point hardware or with less than about 64 MB RAM. Plaits needs fast
  float, and a piano roll needs a real screen (see §7).

## 2. What we take from the M8, and what we don't

| M8 | Twill Studio |
|---|---|
| Song → chains → phrases of 16 steps | Arrangement → clips of any length → notes at any position |
| One note per track per step | Chords allowed: each track has 1–8 voices |
| FX commands in phrase columns, tables | Parameter locks on notes, automation lanes, modulators |
| Instrument modulators (envelopes, LFOs) | Same idea: 4 per instrument |
| Grooves (per-step tick lengths) | Groove templates applied at playback (swing and custom feels) |
| Live mode (queue chains per track) | Later: launch clips per track, quantized to the bar |
| Eight buttons, keyboard-free | Same goal: everything reachable from d-pad and buttons |

## 3. Portability, borrowed from LGPT and fixed where it bit us

LGPT ports to about 20 platforms because the application never talks to the OS directly: everything
goes through small adapters (system, audio, display/events, files, MIDI, timer). We keep that, and
change three things:

1. **Shared code places the picture.** On the Brick Pro, LGPT's SDL2 adapter drew 960×720 into the
   top-left of a 1024×768 screen and never cleared the rest, leaving a band of garbage. Here the
   adapter only presents a finished full-screen buffer at the device's native resolution.
2. **Button maps per device, not per build:** a table of known controllers (by SDL GUID or name) plus
   an override file, instead of one `mapping.xml` per platform zip.
3. **A headless platform from day one,** so the whole app can be tested and rendered to WAV without a
   display.

## 4. Layers

```
┌───────────────────────────────────────────────────────────┐
│ app/      views (piano roll, arrangement, instrument,     │
│           mixer, browser), editing, undo                  │  portable C++
├───────────────────────────────────────────────────────────┤
│ model/    Project, Track, Clip, Note, Lane, Instrument    │  portable
│ engine/   transport, scheduler, voices, mixer, FX         │  portable, real-time safe
│ gfx/      2D rasterizer, fonts, layout, presenter         │  portable
├───────────────────────────────────────────────────────────┤
│ platform/ Platform interface (abstract)                   │
│   sdl2/   desktop + Linux handhelds (Brick Pro, RG35XX…)  │
│   headless/ tests, WAV render, benchmarks                 │
│   later:  web (Emscripten+SDL2), iOS/Android, more        │
│           Linux handhelds                                 │
└───────────────────────────────────────────────────────────┘
third_party/  plaits/, stmlib/, clouds reverb (MIT), dr_wav and dr_flac (public domain)
```

Rules that keep it portable:

- **C++17, no exceptions, no RTTI.** The RG35XX Plus SDK that LGPT uses is GCC 11 and KNULLI's own
  build is GCC 13; Emscripten is Clang.
- **No OS headers outside `platform/`:** no `<thread>`, `<filesystem>` or SDL includes in core code.
  The core works with either a threaded audio callback or a single loop that calls `Engine::render()`.
- **Nothing on the audio thread allocates, locks or does I/O.**
- **Single-precision float** in the audio path.

## 5. The platform interface

One abstract class per platform; optional features answer "not available".

```cpp
class Platform {
public:
  // Audio. The engine always runs at 48 kHz; a shared resampler bridges other device rates.
  virtual bool audio_open(int preferred_rate, int preferred_block, AudioCallback cb, void* user) = 0;
  virtual int  audio_rate() const = 0;
  virtual void audio_close() = 0;
  virtual bool audio_input_open(...)        { return false; }   // mic / USB in, for sampling

  // Display: present a finished full-screen buffer at native resolution.
  virtual Size  display_size() const = 0;
  virtual float display_dpi() const          { return 0; }      // 0 = unknown; used for UI scale
  virtual void  present(const uint32_t* pixels, int stride, const Rect* dirty, int n_dirty) = 0;

  // Input: abstract buttons, plus optional pointer/touch and keyboard.
  virtual void poll_input(InputState& out) = 0;
  virtual bool has_pointer() const           { return false; }
  virtual bool has_keyboard() const          { return false; }

  virtual FileSystem& files() = 0;           // rooted at a data folder: projects/, samples/, settings
  virtual MidiPort* midi()                   { return nullptr; }
  virtual int  max_tracks() const = 0;      // from benchmarks, see §6 "Track count"
  virtual int  max_voices() const = 0;
  virtual uint64_t micros() const = 0;
  virtual void log(const char* msg) = 0;
};
```

The **SDL2 adapter** covers macOS, Linux, Windows and the Linux handhelds. On the Brick Pro it uses
KNULLI's system SDL2 (2.32.8, `mali` video driver, PipeWire behind ALSA `default`), so nothing needs
bundling. The **headless adapter** plays scripted input and writes audio to WAV and frames to PNG.

Later, in this order of likely effort: **other Linux handhelds** (RG35XX-class devices with 640×480 or
larger screens; mostly button maps and packaging, since the SDL2 adapter already covers them),
**Web** (Emscripten + SDL2, running in a browser), and **iOS/Android** (touch makes the piano roll
easier, not harder). A DAW plugin build is not planned.

## 6. Audio engine

### Time

- The engine runs at **48 kHz**, Plaits' native rate.
- Musical time is in **ticks, 960 per quarter note** (fine enough for triplets, quintuplets and
  off-grid playing recorded from MIDI). Positions are 32-bit, which covers more than 100 hours at 300 BPM.
- The scheduler works in **12-sample blocks** (Plaits' `kBlockSize`) on a fixed grid counted from
  the engine's start. A note starts at the beginning of the block that contains its exact time, so
  up to 0.25 ms early. Because the grid doesn't depend on the device's callback size, a song
  renders bit-identically at any block size (tested). Instruments that can start mid-block (the
  sampler) may later start on the exact sample.
- **No drift:** the tick position is computed from an anchor (the tick and sample of the last
  tempo change, locate or loop) rather than summed block by block.
- **Grooves** shift note start times at playback by a per-grid-position amount, so the stored notes stay
  on the grid and the feel can be swapped.

### Threads and edits

The model is variable-sized (clips hold any number of notes), so the engine can't simply share
fixed-size arrays with the UI. Instead:

- **The UI owns the editable project.** Every edit is a command, which gives undo/redo for free.
- **The audio thread plays immutable snapshots.** After an edit, the UI builds a compact playback
  copy (notes sorted by start, lanes as breakpoint arrays) and sends a pointer through a lock-free
  single-producer/single-consumer queue. The audio thread swaps it in at the start of its next
  callback and sends the old one back through a second queue for the UI to free. As built in M2,
  the snapshot covers the whole project, which is quick at handheld sizes; rebuilding only the
  changed clip is an optimization for later if big projects need it.
- Small, frequent changes (moving a fader, turning a parameter) go through the same queue as
  parameter messages, without rebuilding anything.
- **Audio → UI:** the playhead position, which notes are sounding (to light them up), meters.
- **Samples** load and convert on a loader thread (or the main loop where there are no threads) and are
  handed to the engine only when complete.

### Track count

- **Set per platform**, from that platform's benchmarks (M1): each platform declares how many tracks
  and how many voices in total it can run (`Platform::max_tracks()`, `max_voices()`), with a
  safety margin. A desktop might allow 16 tracks, a handheld 8.
- **The file format allows up to 16 tracks**, so a project made on a bigger machine opens anywhere.
  If it has more tracks than the device runs, the extra tracks load, show and can be edited, but are
  marked as not playing on this device. Nothing is dropped from the file.
- Voices are the finer limit: when the total across tracks would exceed `max_voices()`, the oldest
  releasing voices are stolen first. *As built:* when that cuts off a held note, or a tail still
  above -30 dB, the top bar shows "Voices full" (in place of "Playing") for 1.5 s. The demo song
  steals only quieter tails, so it never shows.
- *As built:* a project always has the platform's number of tracks; they aren't added or deleted.
  The track settings panel (X on a track's name) opens on Engine, under **Name**: Left/Right steps
  through a list of part names (Drums, Bass, Chords, Lead...), and A opens the letter editor the
  song's name uses, where X gives the next part name. It has **Position** (Left/Right moves the
  track one place, R + Left/Right to the top or the end) and **Clear track** (no clips, default
  sound, named "Synth"). Renames, moves and clearing are single undo steps. Names say what a track
  is for, never where it is: new songs name their tracks for the parts they're set up for. A track's colour is its own (an index into the
  theme's set, saved in the file), so it keeps it when moved.

### Voices and polyphony

- Each track has **1–8 voices** (default 1 for Plaits, 4 for Sampler), with oldest-note stealing.
  Mono tracks get legato and glide, as on the M8.
- *As built:* in a kit, Voices counts per drum (per note): drums never cut each other off, even at
  one voice, while a drum hit again cuts off its own last hit. The row reads "Voices per drum".
- *As built (2026-11-15):* **kit drums are one-shots.** A drum plays to the end of its sample however
  short its note: the note's end (or a live key's release) doesn't release it. It stops early when its
  choke group cuts it, when it is stolen (at one voice per drum, by its own next hit) or at Stop. The
  ADSR still shapes every hit through its attack, decay and sustain. Single and slice modes still end
  at the note's end.
- *As built:* on a one-voice macro track every note retriggers, including a note that starts
  exactly where the last one ended. A note with **glide** slurs into the next pitch with no
  retrigger. **Voices: Legato** (one step below 1; stored as 0) slurs every overlapping or touching
  note without sliding. In ADSR mode the low-pass gate stays open and the ADSR is a plain amplifier
  after it; a retrigger restarts the attack from zero, ramping down to it over one grid block.
- *As built (device):* the whole engine averages about 37% of one Brick Pro core on the demo song
  with effects. Plaits itself is about 2% per voice; that song keeps about 14 voices sounding.
  Handhelds use 512-frame audio blocks on a SCHED_FIFO thread: 256 frames underran.
  The Audio buffer setting picks Low (512 on a handheld, about 11 ms) or Safe (1024, about 21 ms).
  Under KNULLI's PipeWire, an app asking for 512 drops the whole graph to a 512 quantum, and the
  Brick Pro's speaker output then runs short a few times every 10 minutes (one quantum of
  silence); at 1024 it doesn't. So Safe is the default on handhelds where PipeWire runs, Low
  elsewhere, until the user picks one. While a song plays, the app also keeps the device awake
  (the platform's keep_awake: KNULLI's battery saver otherwise mutes after 5 minutes without a
  button press).
- *As built (device):* the Brick Pro's codec compresses its output in hardware (the device tree's
  `dacdrc_cfg`; bit 29 of DAC_DAP_CTL), which made beats pump. While audio is open the SDL2
  platform turns that bit off (`OutputCompressor`, Linux, this codec only) and restores it on close.
- **Plaits is monophonic per voice**, so a 4-voice Plaits track runs four `plaits::Voice` instances.
  Its chord engine is still there for one-voice chords.
- **CPU budget (measured in M1 with `twill_macro_lab bench`, 256-frame blocks).** On the Brick
  Pro (Cortex-A53 at 1.6 GHz) one macro-oscillator voice costs 0.6–2.7% of one core on average,
  depending on the engine (Chiptune Arp and Speech cheapest; Formant, Particles and Plucked string
  dearest), with worst-case blocks up to 5.2% (Swarm). That is about 36 voices per core for the
  heaviest engines. The Brick Pro's limit is set at **16 voices** for now, under half of one core
  in the worst case, leaving room for samplers, effects and the UI. On an Apple-silicon Mac a voice
  costs at most 0.5% of a core.
- **Handhelds have 32 voices, at most 16 of them on the macro oscillator** (2026-11-21). A
  multisample song needs more than 16: release tails keep their voices after the notes end, and a
  preset with two layers plays two voices a note. Measured on the Brick Pro (engine only, 256-frame
  blocks, average): a sample voice costs about 0.9% of a core, a Swarm voice 2.7%, the effects with
  nothing playing 5%; 16 Swarm voices and 16 sample voices together about 63%. A macro note with all
  16 synth voices busy takes one of those, never a sample's. Two fixes came out of measuring it: the
  drive's soft clip uses `fast_tanh` (within 0.0001 of tanh; std::tanh had made the drive cost as much
  as the rest of a sample voice), and a layered note counts once against the track's voices (its
  layers used to count separately, so a 2-layer preset held half the notes).
- **Speed-ups for the Brick Pro's Cortex-A53** (2026-11-21, engine only, average share of one core).
  The A53 runs in order and glibc's `sin`, `tanh`, `log10` and `pow` are slow on it, so per-sample
  calls to them cost far more there than on a Mac. Changes: the drive's `fast_tanh`; the chorus turns
  one sweep (a rotating sine and cosine) instead of four `sin` calls a sample; the compressor takes
  its logs only above the threshold; each send effect sleeps once nothing is sent to it and its
  tail has died away (the chorus after its 85 ms ring, the delay once its echoes fall below
  -140 dB, the reverb a second after it falls below -120 dB) and wakes with the next block sent;
  a sample voice reads a run of frames with no edge, loop point or crossfade in reach without the
  per-frame checks; the envelope ramp has no divide; a track with no sends skips the send mixing.
  Results: nothing playing 5.4% to 2.2%; Misty Kite (20 multisample voices) 14% to 7.4%; 16 choir
  voices with drive 34% to 12.6%; 8 Swarm voices 27% to 23.5%. Benchmarks for this kind of work:
  render the engine alone with a NullPlatform holding the song's files, cross-built with the
  KNULLI toolchain, run over SSH (the Brick has no `perf`).
- Voice rendering can be spread over cores (tracks are independent until the mixer) if one core isn't
  enough. The core exposes that as "render these tracks" jobs the platform may run in parallel.

### Instruments

Every instrument has the same tail: **source → filter → drive → amp envelope → pan/level → sends**,
plus 4 **modulator** slots.

#### Plaits

- **Naming:** the eurorack repository asks that derivative works not use the "Mutable Instruments"
  name (a registered trademark) and recommends against keeping module names. So the app never calls
  this instrument "Plaits": it needs its own name (to be chosen), and its engines use descriptive
  names ("Virtual analog", "Formant", "6-op FM bank 1", …). The code keeps upstream names internally,
  and the MIT notices stay with the vendored source (`third_party/eurorack/`).
- Built from Mutable Instruments' source (`plaits/dsp` and the `stmlib` parts it uses), **unmodified**,
  pinned to eurorack commit `f4cfa76` (2026-10-12). MIT-licensed, so it doesn't restrict our license.
- **Two hardware ties, worked around without editing their code:**
  1. `plaits/user_data.h` reads custom wavetables and DX7 patches from a fixed flash address. Built with
     `-DTEST`, it returns "no user data" and the six-op FM engines use their built-in banks. Loading
     user banks later needs a small shim.
  2. Plaits tunes against `kCorrectedSampleRate` (47,872.34 Hz, the module's real clock), so at a true
     48 kHz it plays about 4.6 cents sharp. We subtract that from the `note` we pass in.
- **Each voice:** a `plaits::Voice` plus a 16 KB working buffer, producing `out` and `aux` as 16-bit
  samples.
- **Parameters:** engine (by name), harmonics, timbre, morph, decay, LPG colour, the three internal
  modulation amounts, and output (Out / Aux / Mix).
- **Envelope modes:**
  - **LPG** (default): a note holds Plaits' trigger input high for its length, as a **gate**, and its
    own envelope and low-pass gate shape the sound, as on the module. It must be a held gate, not a
    short pulse: the six-op FM engines sustain their DX7-style envelopes only while the gate is high,
    and with a pulse most of their patches are silent (found in M1).
  - **ADSR:** the low-pass gate is held open and our amp envelope is applied after the voice, as a
    plain amplifier, so note length and release behave like a normal synth. The ADSR also takes the
    internal envelope's place for the three modulation amounts (fed in as if patched into the FM,
    timbre and morph inputs), except where two engines use those amounts for something else:
    speech's FM and morph amounts and chiptune's timbre amount. Decay and LPG colour then do
    (almost) nothing, so the sound view shows them only with the gate, under Amp.
- **Chiptune Arp** (engine 7): with its trigger patched, the engine is an arpeggiator that steps
  once per trigger, so Twill clocks it itself while a note is held, at 1/8, 1/16 (default), 1/32 or
  1/64 notes of the song's tempo (the **Arpeggio** row, saved per track), and restarts it at each
  note so the note begins on its own pitch (a marked change in the vendored engine,
  `third_party/eurorack/README.md`). Harmonics picks the chord (octave, fifth, sus4, minor, minor
  7th, 9th and 11th, 6/9, major 9th and 7th, major) and Timbre the pattern (up, down, up-down or
  random, over 1, 2 or 4 octaves); the sound view names both and steps them one at a time (the
  Chords engine's Harmonics too). Triggered, the engine passes the low-pass gate by, so under the
  gate setting Twill applies the gate's part: each trigger rises to full in 1 ms and dies away at
  the Decay's rate, as the other engines do.

#### Sampler

- One sample per instrument, pitched around a root note; start, end, loop (off / forward / ping-pong)
  and loop points, reverse, fine tune.
- **Slice mode:** 2–64 equal slices or slices at transients, mapped to consecutive notes, for breaks
  and drum kits.
- **Drum kits** are a sampler in **multi-sample mode:** up to 32 samples, each on its own note, with
  per-sample tune, level, pan and choke group. The piano roll then labels its rows with the sample
  names (§7).
- Cubic (Hermite) interpolation; linear as a fallback on weak targets.
- **As built in M4:** single, slice (equal slices, or at transients: see below) and kit
  modes. Kits have **16 slots** on notes C2 to D#3 (MIDI 36–51), each with its own sample, tune,
  level, pan and choke group (four groups). Start, end, forward and ping-pong loops and reverse.
  A note name at the end of a file name ("Pluck C3") sets the root note when the sample is picked.
- *As built (2026-11-14):* **slicing at transients.** "Slice: At transients" cuts the region
  between Start and End where sounds begin instead of into equal parts; "Most slices" (the Slices
  setting) caps how many, keeping the strongest onsets. Onsets are found once, when a sample loads,
  with a spectral transient detector (`find_onsets`, at a fixed sensitivity of 0.8): 5 ms frames,
  the rise in energy and above all in treble over the frames before, a peak standing well above the local median and loud enough to matter, at least 30 ms
  apart; each cut sits just before the attack at a zero crossing. Each onset keeps a strength (how
  far it stands above its surroundings, favouring loud hits) for the cap.
  Slice n plays on root + n as before, and the waveform shows the cuts.
- **Factory kit (2026-11-15):** 22 drum samples recorded for the app from an analog drum machine
  (kicks, snares, claps, hats, toms, cymbals, cowbell, FM percussion), in `assets/factory/` as 16-bit
  48 kHz mono WAVs named by note ("36 Kick.wav"). They're built into the binary
  (`cmake/embed.cmake`) and written to `samples/Factory/` on first launch, so no package carries a
  sample folder. `load_factory_kit()` lays them out in General MIDI order from note 36 with levels,
  hats in choke group 1, and the open hat as the long hat take with a slot decay (the ride is the
  same take in full); "Load the factory kit" and new songs use it. The snare has a light EQ (less
  166/211 Hz body, more top) and Clap 2 is a snare made into a clap (layered attacks). No
  third-party samples. Until 2026-11-15 the factory sounds were rendered from the macro oscillator.
- **New songs** start with the factory kit on track 1 ("Drums", one voice per drum) and macro
  oscillators on tracks 2–8 (virtual analog, wavetable, formant, plucked string, string machine,
  two-op FM, analog + filter). The demo song (100 BPM, C minor) is the same kit with a four-bar
  groove, under bass, chords, lead, pluck and pad. The UI tests drive a fixed copy of the earlier
  demo, so the demo can change without touching them.

#### Multisamples (planned 2026-11-20, built 2026-11-21)

For small sample libraries (tens of MB: a piano, keys, a plucked or bowed instrument, with a few
velocity layers and round robins), read from a preset file a library already ships with.

- **One model, readers on top.** A **multisample** is a list of **zones**, each: a sample file, a key
  range, a velocity range, a root note, tuning (semitones), level (linear), pan (-1..1), start and
  end, loop (off / forward, with points), and a round-robin place (sequence mode, length,
  position). A reader turns a preset file into this list; everything after it (playing, memory,
  the screens, the song file) is written once, for every format.
- **Sampler mode "Multi"**, beside Single, Slices and Kit. The sound view picks a preset file in the
  file browser (`.dspreset` first, `.sfz` later) and shows its name, its zone count, its key range
  and anything it couldn't use. Tune, level and pan apply to the whole instrument; the track's
  filter, ADSR, modulators and sends work as for any sampler track. Zones aren't edited in Twill.
- **Playing a note:** every zone whose key and velocity ranges hold the note plays (layered zones
  are layered voices, counted against the track's voices), at note - root + tuning semitones, with
  its level and pan; the note's velocity sets the level as on any sampler track, or as the preset's
  velocity tracking says. Round robins: a zone in a sequence plays when its position comes up
  (`round_robin`), or a random one does (`random`: never the same twice running; `true_random`).
- **The envelope:** when a preset gives one amp envelope for all its zones, choosing it sets the
  track's ADSR to that (one undoable change), since a piano's release matters; afterwards it's the
  track's to change. Presets with different envelopes per group use the first, and say so.
- **Memory.** Today a sample is kept as stereo float at 48 kHz, 384 KB a second. Every sample (not
  only zones: one path for all) moves to **16-bit at the file's own channel count**, resampled to
  48 kHz on load as now: 96 KB a second for mono, 192 for stereo, a quarter or half, with its noise
  floor at about -96 dB. A platform sets a **budget** for loaded samples, to be found by trying
  libraries on the devices (a starting guess: 256 MB on a handheld with 1 GB); a preset that would
  go over it isn't loaded, with a message saying how big it is. Zones share loaded samples by path,
  as tracks do.
- **Loading** happens on a thread of its own, with progress in the hint bar ("Loading 23 of 60
  samples"); notes play once it's done. Today's loads are quick enough to run on the UI thread; 50
  MB from an SD card isn't.
- **FLAC**, which libraries use a lot, is read with `dr_flac` from the same `dr_libs` as `dr_wav`.
  WAV and AIFF are already read.
- **Songs** refer to the preset by path (track chunk version 10), with its samples beside it as the
  library has them, nothing copied into the song. A missing preset or sample is said once, when the
  song loads, and its notes are silent.

*As built (2026-11-21):*

- The zone model (`model/Multisample.h`), the DecentSampler reader (`model/DsPreset.cpp`), the SFZ
  reader (`model/SfzPreset.cpp`) and `read_multisample()`, which picks one by file extension.
  Besides the fields above, a zone has key tracking (with tuning applied before it, DecentSampler's
  way, or after, SFZ's), velocity tracking, one-shot playback, a random range (SFZ's
  `lorand`/`hirand`: one random number a note), and "loop if the file has loop markers" (SFZ's
  default when no `loop_mode` is given).
- Multi mode in the engine: layers, round robins, random groups and ranges, polyphony in notes, zone
  and WAV loop markers. A new snapshot (any edit) leaves the voices of a preset still in use playing.
- **Loading** (`app/PresetLoader`): one preset at a time on a thread of its own, the rest queued.
  The thread reads the preset and decodes its samples, sharing ones already loaded; the UI thread
  takes the finished preset, adds its samples to the cache, and rebuilds the snapshot, so notes play
  from then on. Until then the track's Multi zones are empty (silent). The hint bar says "Loading
  Grand: 23 of 60", and the sound view shows a progress bar. The web build has no threads and loads
  in place. Rendering a song (export, stems) waits for its presets first. A preset no track names is
  forgotten, its samples freed as any others are; using it again loads it again.
- **Picking a preset** (`App::pick_multisample`) keeps the track's old one playing while the new one
  loads; once it's ready the track takes it, with its amp envelope, as one undo step. A preset that
  can't load leaves the track as it was and says why in the hint bar.
- **The notice:** if the preset has things that aren't played (or samples that couldn't be read),
  a card over the view says what, once, when it's picked; A closes it (B and X too), and the view
  gets no buttons while it's up. (This replaced a "Not used" row in the settings list, which read
  as a setting and was cramped.)
- **The sound view** in Multi mode: Preset (A opens the browser, which lists `.dspreset` and `.sfz`
  files and folders), Zones ("66, A1 to D7, 2 missing", or "Loading...", or why it can't load), Fine
  tune. The picture on the right is a **zone map**: keys across, velocity up, each zone a tinted
  rectangle (layers and round robins overlap darker), a keyboard below with a dot on each key a
  sample was recorded at, octave names, the preset's memory, and all samples' memory against the
  budget.
- **The zone map lights the zones sounding** (solid in the track's colour, outlined): each voice
  records its zone, and the engine publishes them after every callback (`Engine::sounding_zones`,
  lock-free), which the sound view checks each frame and redraws on a change. It shows velocity
  layers and round robins at work, and why a note is silent.
- **Library bundles** (DecentSampler's `.dsbundle` folders, as libraries are usually unpacked): in
  the preset browser a bundle with one preset is listed as that preset, by the bundle's name ("Basic
  Piano"), and A picks it; one with several is a folder that opens to just its presets (not its
  Samples and Images folders). Reopening the browser on a one-preset bundle's preset starts beside
  the bundle.
- **Memory budget** (`Platform::sample_budget()`): on a handheld, set once at start from the memory
  the kernel says is free (`MemAvailable` in `/proc/meminfo`): 60% of what's left after keeping 160
  MB back for the system and the app, from 64 MB to 1 GB (`Sdl2Platform::budget_for`), logged at
  start. Without a reading, 256 MB. Computers have no limit. It counts every loaded sample (kits and
  single samples too), and a preset is refused when its new samples would go past what's left, with
  a message saying so. The About panel shows "Samples: 120 of 380 MB".
- Still to come: `.dslibrary` zips, release triggers (if ever), and making multisamples of one's own
  (below).

**DecentSampler presets (`.dspreset`)**, written from the format's public documentation (its
developer guide and schema):

- XML: `<DecentSampler><groups><group><sample .../></group></groups></DecentSampler>`. Parsed with
  pugixml (MIT), vendored in `third_party/`.
- Read from `<sample>`: `path` (relative to the preset), `rootNote`, `loNote`/`hiNote`,
  `loVel`/`hiVel`, `start`/`end`, `tuning`, `volume`, `pan`, `loopStart`/`loopEnd`/`loopEnabled`
  (and a WAV's own loop markers when no points are given), `seqMode`/`seqLength`/`seqPosition`,
  `attack`/`decay`/`sustain`/`release` (and their `ampEnv` spellings).
- Inheritance as documented: most attributes set on `<groups>` or `<group>` apply to the samples
  below unless a sample sets its own. Volume doesn't inherit; the three levels multiply (`volume`, or
  `globalVolume` / `groupVolume`), in linear or in dB ("-3dB"). `pan` inherits, and `globalPan` and
  `groupPan` are added to it, clamped. Tuning adds up (`globalTuning`, `groupTuning`, `tuning`), and
  so does transposing (`globalTranspose`, `transpose`). Groups with `enabled="false"` are left out.
- **Not used, and named in the notice when the preset is picked:** effects, the UI and its controls,
  modulators, buses, MIDI mappings, tags, note sequences, the arpeggiator, oscillators, release and
  legato triggers (`trigger` other than `attack`: release triggers, for a piano's key-off and pedal
  noises, aren't planned for now), key switches and CC ranges, MIDI
  channel ranges, glide and the monophonic settings, sample slicing and tempo-synced loops. A preset
  still plays its sample zones without them.
- Libraries come as a folder; `.dslibrary` files (zips) are a later step (miniz, MIT).

Two more of its documented attributes are played: `pitchKeyTrack` (with `tuningIgnoresKeyTrack`) and
`ampVelTrack`. **Loop crossfades** too (2026-11-21): `loopCrossfade` (frames, inheriting) and
`loopCrossfadeMode` (`equal_power`, the default, or `linear`); SFZ's `loop_crossfade` (seconds) the
same way. Over the loop's last crossfade-length frames the voice also reads the audio that far
before the loop's start, fading it in as the end fades out; at the loop's end it goes back to the
start, which carries on from what was fading in. The crossfade is cut to fit the loop and the audio
before it.

**SFZ (`.sfz`)**, written from the format's public documentation (sfzformat.com):

- Comments (`//`, `/* */`), `#define $NAME value` (replaced wherever it comes after, longest names
  first) and `#include "file"` (relative to the `.sfz`'s folder, up to 8 deep).
- Headers `<control>`, `<global>`, `<master>`, `<group>` and `<region>`: a region takes the opcodes of
  the group, master and global above it, its own winning; a header clears the levels below it. An
  opcode's value runs to the next opcode, header or line end, so sample paths may hold spaces.
- `<control>`: `default_path`, `note_offset`, `octave_offset`, `set_ccN` (`set_hdccN`).
- Read: `sample`, `lokey`/`hikey`/`key`, `pitch_keycenter` (note numbers or names, "c4" = 60),
  `lovel`/`hivel`, `tune` (cents) and `transpose` (both change the pitch, not the mapping),
  `pitch_keytrack`, `volume` and `amplitude` (with their `global_`/`master_`/`group_` forms,
  multiplied), `pan`, `amp_veltrack`, `offset`/`end`, `loop_mode` (`no_loop`, `one_shot`,
  `loop_continuous`; `loop_sustain` plays as continuous) and `loop_start`/`loop_end` (or
  `loopstart`/`loopend`), `seq_length`/`seq_position` (a round robin per group header),
  `lorand`/`hirand`, `trigger` (only `attack` plays), and `ampeg_attack`/`decay`/`sustain` (%)/
  `release`, the first region that sets them being the instrument's.
- Key switches: only one articulation plays, `sw_default`'s, else the lowest `sw_last`. Controller
  ranges (`loccN`/`hiccN`) are read as if each controller were at its `set_ccN` value, else 0 (a
  sustain pedal up); regions a controller triggers (`on_loccN`) aren't played.
- Listed as not used, by kind: filters, EQ, LFOs, other envelopes, effects, curves, velocity curves,
  crossfades, pitch bend, voice muting (`group`/`off_by`), polyphony limits, release and legato
  triggers, built-in waveforms (`*sine`...), and any other opcode ("other settings").

**SF2** isn't planned: most good SoundFonts are large General MIDI sets, which suit neither the
handhelds' memory nor this model's scope.

**Tests** use presets made for them, with generated samples: key and velocity mapping, layers,
each round-robin mode, attribute inheritance and the volume and pan rules, dB values, a missing
sample, ignored elements, the memory budget, FLAC decoding, and the song file round trip; SFZ's
headers, defines, includes, note names, key switches, controller ranges, one-shots, key and
velocity tracking and random ranges; loading in the background (a preset ready later reaches the
engine without an edit; one no track uses is let go), picking a preset and its notice; and held
notes playing on through an edit.

**Making multisamples of one's own (planned, not soon).** Kits can be made in Twill; multisamples
should be too, simply: no SFZ- or DecentSampler-sized feature set. A sketch: a screen of its own,
reached from Multi mode's preset row ("New multisample"), listing zones as rows (sample, root, key
range, velocity range, level, tune). Adding samples from a folder maps them by the note in their
names ("Pluck C3.wav", as the Single mode's root already does), spread to fill the keys between
them; velocity layers from a second folder or a name suffix. Saved as a small `.dspreset` (so it
also opens in DecentSampler) beside its samples, written with pugixml. Editing a library's own
preset would save a copy, never the original.

**Sound view pictures for the envelope.** While one of the amp ADSR's settings is chosen (any
instrument), the picture on the right is the envelope's shape: a linear attack, then exponential
decay and release as the engine makes them, each stage as wide as its time on a log scale, the held
part a fixed stretch, the chosen stage filled in the track's colour and its value under it. While
the low-pass gate's Decay or LPG colour is chosen, the picture is a struck note through the gate,
worked out block by block with the engine's own gate formulas: its level, and its brightness (how
open the filter is), with how long the note lasts. Chiptune Arp shows the app's own envelope for it
(no brightness); engines that make their own envelope say so instead.

#### MIDI Out

- Sends the track's notes, velocities and two assignable CC lanes to a MIDI channel, for sequencing
  external synths over USB MIDI. The Brick Pro's kernel has the USB MIDI class driver built in.
- **As built in M5:** notes and velocities on a channel (1–16), with note offs timed by the engine
  and flushed when the transport stops.
- *As built (2026-11-14):* two controllers per track, CC A and CC B (CC 1 and 74 to start; any
  number 0–127, set in the sound view). Automation lanes for them send a CC when the lane's value
  changes (a ramp sends each step once), on the track's channel, from the start of playback. A note
  can lock either controller: its value goes out just before the note and holds until the note ends,
  then the lane takes over again. The lanes and locks show the controller's number ("CC 74").
- *As built (2026-11-14):* adding a note on a MIDI Out track auditions it on the device. Notes
  played into a MIDI Out track from a MIDI keyboard go out on the track's channel to every device
  except the one they came from (a synth plays its own keys, and one that echoes its input would
  loop); they still record into the clip. Each incoming message carries its device
  (`MidiMessage::source`) for this.

#### Filter, drive, modulators

- **Filter:** state-variable low-pass, band-pass and high-pass (from `stmlib`), cutoff and resonance;
  later a ladder model.
- **Drive:** soft clipping before the amp envelope.
- **Modulators (4 per instrument):** ADSR or AHD envelope, LFO (shapes, free or tempo-synced, retrigger
  per note or free-running), and "note tracking" (pitch or velocity mapped to a parameter). Each has a
  destination parameter and a depth. They run per voice at 12-sample control rate.
- **As built in M5:** the filter is `stmlib`'s SVF (12 dB/octave; low-pass, band-pass, high-pass) and
  drive is gain-compensated tanh. The amp envelope is an ADSR; the macro oscillator can use it or
  its own low-pass gate. Modulators: an attack/decay envelope, an LFO (sine, triangle, saw, square,
  random; free 0.05–20 Hz or synced 4 bars to 1/16), velocity and key tracking, each to pitch
  (±24 semitones) or a continuous sound setting, with a signed depth.
  Order of application: track setting, then automation lane, then the note's lock, then modulators.
- *As built (0.2.0):* each LFO has a **Phase** setting. *Each note* (the default) restarts it at
  every note-on, per voice. Otherwise there is one LFO per track that runs across notes: a free LFO
  runs from when playback starts (so a song plays the same each time) and keeps going while
  stopped; a synced one follows the song position, so a 1-bar LFO starts its cycle on every downbeat
  wherever the notes fall, and jumps with the song at a loop or a locate (when stopped it runs on
  at the tempo). A legato note (one-voice macro track, legato or glide) carries its modulators on
  as it does the amp envelope: the LFOs keep their phase and the envelope modulator does not
  restart.

### Mixer and effects

- Per track: level, pan, mute, solo, and sends to **chorus**, **delay** (tempo-synced, ping-pong) and
  **reverb** (Clouds' reverb from `clouds/dsp/fx`, MIT).
- **Master:** 3-band EQ, compressor, and `stmlib`'s limiter.
- Effect parameters can be automated like anything else.
- **As built in M5:** sends are taken after the track's level and pan. Chorus: two swept taps per side.
  Delay: ping-pong, 1/16 to 1/2 including triplet and dotted values, damped feedback. Reverb: Clouds'
  reverb, vendored unmodified and fully wet on its bus (it keeps its 12-bit delay memory, part of its
  character). The effects run every block, so tails ring on after a send is turned down; with nothing
  sent they add exact zeros. Master: three-band EQ (bypassed when flat), a compressor (off by default;
  threshold, ratio and release in the effects panel; stereo-linked peak envelope, 10 ms attack,
  automatic makeup of half the full-scale reduction; its gain reduction shows on the master strip),
  level, and a tanh soft limit. PROJ version 5 stores the compressor.
- *As built (2026-11-14):* the tanh soft limit is replaced by a **look-ahead peak limiter**
  (stereo-linked, 64 frames = 1.3 ms of look-ahead, ceiling -0.3 dBFS, 100 ms release), last on the
  master after the level. Below the ceiling it changes nothing. It's on by default; switched off
  ("Limiter" in the effects panel, PROJ version 6) the master goes through the old tanh soft clip
  instead, still through the same delay so switching doesn't jump. `stmlib`'s limiter wasn't used:
  it scales everything by 0.8 and lets the first peaks through. A fast compressor followed by a
  clip was the other option, but without look-ahead every attack reaches the clip. MIDI out waits
  the extra 1.3 ms too.
  The whole master chain now runs a grid block at a time, so automation of it lands the same at
  any callback size.
- *As built (2026-11-14):* lanes automate the effects and master too: reverb size and tone, delay
  time and feedback, chorus rate and depth, the three EQ bands and the master level. They're in
  every track's lane menu, after the track's own settings; a lane for one of them on any playing clip
  sets it for the whole mix (if two tracks automate the same one, the later track wins). The master
  level ramps across each audio callback so automating it doesn't click. The compressor isn't
  automatable.

### Samples

- WAV and AIFF, 8/16/24-bit and float, mono or stereo, via `dr_wav` (0.14.6 reads AIFF too, so no
  separate reader was needed), and FLAC via `dr_flac` (2026-11-20); resampled to 48 kHz on load
  (Hermite). *As built (2026-11-20):* kept as **16-bit at the file's own channel count** (mono stays
  mono), a quarter of stereo float's memory for mono and half for stereo; until then, stereo float.
  *As built (2026-11-14):* a loaded sample stays in memory while any track names it (whatever the
  track's instrument, so switching back doesn't reload). Once none does, it's freed when the engine
  has let go: it's playing a snapshot built without it (sampler voices on a sample a new snapshot
  drops stop when it arrives), nothing else holds it (a browser preview, the waveform shown), and two
  audio callbacks have passed since, so a preview command has been applied. The app checks twice a
  second. Undo brings the name back and the sample loads again.
- A project refers to samples by path relative to its folder. **Save as bundle** copies every used
  sample into the project's `samples/` folder, so a project moves between devices as one folder.
- Later: sampling from the device's input (the Brick Pro has a built-in mic and supports USB audio)
  and resampling the master output.

## 7. Interface

### Drawing: native resolution, not a pixel-art canvas

A clean piano roll needs thin lines, readable small text and smooth zooming, so the UI always uses
the **highest resolution the device offers**: the panel's native mode on handhelds (1024×768 on the
Brick Pro), full-screen at the display's native mode on desktop, and the full backing-pixel resolution
on high-DPI screens (a Retina window draws at 2× its point size). It is never a small canvas scaled up.
The SDL2 adapter asks for the largest display mode and a high-DPI window, and reports the real pixel
size back.
- *Portrait panels* (a portrait panel in a landscape case, like the RG28XX's): the app draws
  landscape and turns the picture on its way out, the way the kernel says the panel is mounted
  (`fbcon=rotate:` on the boot command line, else the DRM panel's `panel_orientation`, the same
  sources PortMaster reads), else 270°. `TWILL_ROTATE=0/90/270` overrides it; the RG28XX launcher
  sets 270, the direction checked on the device, until the kernel's answer has been seen in its log.

- **gfx/** is a small software rasterizer of our own: filled and rounded rectangles, lines, alpha
  blending, clipping, bitmap blits. It needs no GPU, so it ports anywhere a framebuffer can be shown.
- **Fonts** are pre-rendered at build time from open-licence fonts (OFL: Inter, and Barlow Condensed
  for names and labels) into antialiased bitmap atlases at several pixel sizes, with each size's
  capital height for centring. There's no FreeType or font file parsing on the device.
- **Layout in points:** views are laid out in logical units times a UI scale chosen from the screen
  size and DPI (Brick Pro about 1.5×, desktop user-adjustable). Views get more room, not bigger
  pixels, on larger screens: a wider screen shows more bars, a taller one more octaves.
- **Only changed regions are redrawn** (the playhead, a moved note, a meter). On the Brick Pro a full
  redraw is 3 MB per frame; dirty rectangles keep CPU and battery use down. The presenter passes
  them on to the platform.
- **Memory bandwidth is the limit on handhelds, not arithmetic** (measured in M0). On the Brick Pro
  one opaque full-screen layer costs about 1 ms and a blended one several more, about 30 times the
  Mac. So views paint each pixel as few times as possible (each area fills only its own
  background), and full redraws are done in horizontal strips of about 32 rows, so all the layers
  of a strip are drawn while it is still in the CPU cache. M0's full frame went from 9 ms to under
  3 ms on the device this way.
- **Smallest screen:** the layout is designed and tested down to 640×480. Below that (320×240
  handhelds) it would need a compact layout, which is not planned.
- **Look: light by default.** A light, paper-like background with fine grey grid lines and bar
  lines a step darker; scale rows as a faint tint; notes as solid fills in one clear colour per track,
  with velocity as saturation rather than glow. No neon, gradients or glow effects. Contrast is
  chosen for reading a handheld screen in daylight.
  *Changed 2026-11-16, the paper look:* cream paper (#F5EFE1) and near-black ink, with firm ink
  rules under the top bar and rulers. Names, tabs and labels are in Barlow Condensed (bold, mostly
  capitals, small labels letter-spaced); values and running text stay in Inter. Text is centred on
  the capitals' height. The current tab and a selected list row are solid ink blocks with paper
  text. The hint bar is ink in both themes, each button drawn as a keycap. The cursor is four
  viewfinder corners, in every view; the playhead is an ink line with a marker in the ruler. Track
  colours are muted (the same hues as before, so projects keep theirs), each with a pale fill and a
  deep shade: clips are the pale fill with notes in the deep shade (inverted in the dark theme).
  Columns that stay put while the rest scrolls (track headers, the roll's keys or names, the
  mixer's labels) and panels are sheets a shade lighter than the page, casting a soft shadow. The
  keyboard is flat: cream keys, ink black keys, firm lines between keys. Kit rows are taller (16
  points) for 14-point names. Mockups were judged on the RG28XX's own screen at 640x480 before
  any code changed.
- **Themes:** a small set of colour tokens (background, grid lines, bar lines, scale rows, note fill
  per track, selection, playhead). Light is the default. *As built:* a dark theme (cool graphite with
  off-white ink, the same design; piano keys stay light) is the first row of the Settings panel (Menu). It is an app
  setting, kept in `settings.txt` in the data folder, not in projects and not undoable.

### Views

- **Piano roll** (the main view): one clip of the current track.
  - Time across, pitch down. Bar and beat lines; the grid follows the snap setting (1/4 to 1/64,
    triplets).
  - **Scale mode:** pick a key and scale, then either highlight the scale's rows or fold the roll to
    show only scale notes. Folding makes gamepad editing much faster: up/down moves by scale degree.
  - **Drum mode** for multi-sample instruments: one row per sample, labelled with its name, no black/white
    keys.
  - **Ghost notes:** other tracks' notes faintly behind, for writing parts against each other.
  - **Note colour** shows velocity (saturation), and a small mark shows notes with their own parameter
    values or probability below 100%.
  - **Lanes** under the roll: velocity bars, and any number of automation lanes (pick a parameter, draw
    breakpoints or steps).
- **Arrangement:** tracks down, time across, clips as blocks. Reused clips show a link mark. Loop
  region, markers, tempo.
- **Instrument:** the sound source on top (for Plaits: engine name and a live scope of its output; for
  Sampler: waveform with start, end, loop and slice markers), then filter, drive, envelope, and the
  modulator slots with their destinations.
- **Mixer:** one strip per track with meters, level, pan, three sends, mute and solo; master EQ and limiter.
- **Browser:** projects, samples (with preview), instrument presets.
- **Inspector** (a side panel that opens over any view): the selected note's or clip's properties.

### Editing with buttons

The hard design problem is making a piano roll fast without a mouse. The approach is a **cursor**
plus **held-button modifiers**, the same grammar the M8 uses so successfully (hold Edit + direction to
change a value), mapped onto a handheld's buttons. L and R are the first shoulder buttons (L1, R1).

*Changed 2026-11-15:* **L2 / R2 switch view** (previous / next), in one press. Switching views is the
most frequent move, and Select + left/right needs two hands' reach on devices where Select sits by the
D-pad (RG28XX, RG35XX H, RG351P, RG552, RG34XX); Select + left/right still works. Every aarch64
device PortMaster lists has L2/R2, though reviewers often call them awkward, so nothing depends on
them alone. A tap of L or R on its own no longer changes track (it was easy to hit by accident); L
and R are now only held modifiers. The track is picked in the arrangement, or with Select + up/down
in the sound view. **Select + X** opens the settings panel as Menu does, since Menu is awkward or
taken by the system on some devices and many have no sticks to click; the hint bar names it second,
so small screens show it. (It was Select + L for a day; recording from MIDI, which Select + X used to
arm, is now "Record from MIDI" in the settings panel.) The settings panel starts with **Controls**,
a page listing every button combination by view, since the hint bar shows only what fits (about
four hints at 640x480).

*Changed 2026-11-21: Menu, the main menu, and clip loops.*

- **Menu opens the settings of what the screen shows:** the main menu in the arrangement (the song)
  and the mixer, the open clip's panel in the roll (wherever the cursor is, whatever is selected:
  X on a note could open the note panel by surprise when selected notes were off screen), the
  track's settings in the sound view. Tapping it again puts the panel away. **Select + X** opens the
  main menu from any view (a second press closes it). X on an empty bar in the arrangement opens it
  too. *Since 2026-11-23 Select + X does exactly what Menu does; see below.*
- **Select + X is Menu** *(changed 2026-11-23)*: it opens the settings of what the screen shows,
  the same panel Menu opens in each view, and a second press puts it away. Two ways to one thing are
  easier to learn than a chord that opens something else; and on sndstation handhelds Menu (and
  Select + Start, which the system reports as Menu) is kept for a quit and sleep menu, so Select +
  X is where the screen's settings go there. The main menu is still one step away from any view:
  Menu or Select + X in the arrangement and mixer, and the last row, **Main menu**, of the clip's
  panel (roll) and the track's settings (sound view).
- **On sndstation, Menu quits and sleeps** *(2026-11-23)*. sndstation keeps Menu (FN, or Select then
  Start, which its SDK reports as Menu to every app) for leaving: in its launcher it opens a power
  menu, and a plugin it hosts never gets it. Twill does the same there: Menu opens a small card over
  whatever shows, **Back to Twill**, **Sleep** and **Quit Twill**, the cursor on Back to Twill so a
  stray A is harmless (Up/Down choose, A does it, B or Menu closes it). Back to Twill closes the
  menu, and over a plugin's own screen also leaves it for the Sound page, as the launcher's Close
  does. Decent Sampler's standalone app has the same menu. Sleep saves the song and stops it first. The screen's
  settings are on Select + X alone there, and Controls says so. Other platforms keep Menu as above.
- **The main menu** (it was the "settings panel") is titled MAIN MENU, with the version and commit
  beside the title for testing, and runs in sections as a desktop app's menus do: **Song** (Name,
  New project, Load, Save, Render to WAV, Render stems), **Music** (Tempo, Key, Scale), **MIDI**
  (Record from MIDI, MIDI clock, MIDI devices), **Settings** (Theme, New song names, Audio buffer)
  and **Help** (Controls, About).
- **The clip panel** starts with **Loop length** (it was "Clip length", further down): how long the
  clip's pattern is before it repeats, as against how long the arrangement plays it. Its last row,
  **Main menu**, opens the main menu.
- **A + Y on a clip in the arrangement: no repeat.** A + Left/Right sets how long a placement plays
  the clip, repeating its loop; pressing Y while A is still held makes the clip's loop that long, so
  it plays once. The notes stay where they are (a longer loop has empty bars at its end; a shorter
  one hides later notes, which come back if it's lengthened). Every placement of the clip plays the
  new loop. One undo step.
- **Output boost** (main menu, Settings: Off, +3, +6, +9, +12 dB; kept in `settings.txt`): gain
  before the master limiter, for a device that plays quietly. Twill's mixes peak at the limiter's
  ceiling but average about -21 dBFS RMS (the demo song), some 10 dB under commercial music; on the
  RG28XX under sndstation that was too quiet even with the hardware at full. It's live playback
  only: renders and stems use an engine of their own and stay as mixed.
- **A button that closes a panel is the panel's until it's let go.** B closes a panel on its press;
  its release used to reach the view underneath, and in the arrangement releasing B deletes the
  clip under the cursor.

*Changed 2026-11-16:* **X in the sound view** opens the track's settings, as X on a track header
does in the arrangement (it did nothing there; every other screen's X opens a panel). **Y in the
mixer** plays the track's sound, as Y does in the sound view: Y means "listen" wherever there is
nothing to select (sound view, mixer, sample browser), and stays select in the roll and duplicate
in the arrangement.

*Added 2026-11-14:* **select all** in the roll: Y + A, or "Select all" in the clip panel (X off a
note); A, B, X and copy then work on every note. Tapping **Menu** again puts the settings panel (or
any panel opened from it) away. The settings panel ends with **About**: version, commit (with "+"
when built with uncommitted changes), build date, audio and screen. `cmake/version.cmake` writes
`Version.h` at every build.


| Input | Piano roll |
|---|---|
| D-pad | move the cursor by grid step (left/right) and by row or scale degree (up/down) |
| A (tap) | add a note at the cursor with the last-used length, or select the note under it |
| A held + left/right | shorten / lengthen the note (by grid step) |
| A held + up/down | transpose the note (row or scale degree; with R: octave) |
| B (tap) | delete the note under the cursor |
| B held + d-pad | move the selected note(s) in time and pitch |
| A or B held + L + left/right | fine: resize or move by an eighth of the grid step, off the grid (the cursor stays on it, and finds any note overlapping its cell) |
| X on a note: Start, Length | move or resize by an eighth of the grid (R: a whole step); "Snap to grid" puts the selected notes back on it, and the clip panel's "Snap all notes to grid" does the whole clip |
| Select + Y / Select + R | copy the selected notes (or the one under the cursor) / paste them at the cursor, keeping pitches and spacing; also Copy and Cut in the note panel and "Paste at cursor" in the clip panel. In the arrangement: copy a clip / paste an independent copy at the cursor on any track |
| Y held + d-pad | extend a selection rectangle; then A/B act on all selected notes |
| X | open the inspector for the selection (velocity, probability, glide, parameter locks) |
| L / R held + d-pad | in the roll: the grid (left/right; the view zooms with it, and a message names the new grid) and scroll by page (up/down); in the arrangement: zoom |
| L2 / R2, or Select + left/right | switch view: arrangement, piano roll, instrument, mixer |
| Select + up/down | in the roll: between the notes and the lane strip; in the sound view: previous / next track |
| Menu, or Select + X | the settings of what the screen shows: main menu (arrangement, mixer), clip (roll), track (sound view); the clip's and track's end with a "Main menu" row |
| Start | play / stop (L or R + Start: play from the cursor) |

Most handhelds also have analog sticks; they could add shortcuts (scrolling) but nothing requires
them. On desktop, a mouse and keyboard work as in any piano
roll, and touch works on phones and tablets. **Live input:** with a USB MIDI keyboard connected, play
and record into the clip, quantized or not.

**As built in M3** (to be refined by use on the device):

- **A and B act on the selection** when the note under the cursor is part of it (or there's no note
  under the cursor); otherwise on just the note under the cursor. Each held gesture (a whole resize
  or move) is one undo step.
- **B deletes on release**, and only if the d-pad wasn't used while it was held; B + d-pad moves.
- **Y alone** toggles the note under the cursor in the selection (or clears the selection on an
  empty cell); Y + d-pad selects a rectangle.
- **Zoom is the grid step:** L/R + left/right steps through 1 bar ... 1/64 (with triplets); cells
  keep their width, so the cursor always moves one cell.
- **Global chords:** Select + left/right switches view, Select + B undoes, Select + A redoes,
  L or R + Start plays from the cursor (it was Select + Start until 0.2.0; that's PortMaster's and
  most handheld front ends' "close the app" shortcut, so the app leaves it free). **Menu + Start
  quits** (on a few devices PortMaster closes ports with that same chord, which is just as good).
  **Menu on its own opens the project panel** (tempo, key, scale, files, MIDI clock) from any view; the hint bar says so when
  there is room. (X on an empty bar in the arrangement still opens it too.) *Since 2026-11-21 Menu
  opens the screen's own settings and Select + X the main menu; see above.*
  (Select + up/down changed tempo in M3; since M5 it moves between the notes and the lane strip, and
  tempo is in the project panel.)
- **Start** plays the song in the arrangement and loops the edited clip's placement in the roll.
- **X** opens a side panel: the selected notes (velocity, probability, glide, length), or with
  nothing selected the clip and grid (grid, key, scale, fold to scale, clip length).
- The hint bar at the bottom changes with the buttons held, so the grammar teaches itself.

**Arrangement** (M3): the d-pad moves by track, and along a track an empty bar or a whole clip at a
time (left of bar 1 is the track header): the cursor never stops part way along a clip. A on
an empty bar makes a one-bar clip, A held + left/right resizes a clip by bars (on a header:
changes engine), B deletes, B held + left/right moves a clip, Y duplicates it (linked: the copy
plays the same clip), X opens it in the roll (on a header: the track's settings). Projects
autosave a few seconds after each change and when quitting.

**As built in M4 and M5:**

- **Views:** Arrange, Roll, Sound, Mix, in that order for Select + left/right.
- **Engine** is one list everywhere (Sound view, track settings, A + left/right on a header): the
  macro oscillator's 24 engines, then **Sampler**, then **MIDI out**. A sampler track's settings in
  Arrange have a "Sound settings" row that opens the Sound view, where its sample is chosen.
- **Sound:** a list of the current track's settings (engine, sample, filter,
  drive, envelope, voices, then the four modulators) beside a picture: the engine, the sample's
  waveform with start, end, loop and slice markers, or the kit's sixteen slots. Up/down picks a
  setting, left/right changes it (R: bigger steps), B resets it, A on a sample opens the browser,
  Y plays the sound, L/R changes track.
- **Browser:** folders and WAV/AIFF files anywhere on the device; A opens or picks, Y previews, B
  goes up a folder. Paths inside the data folder are stored relative to it.
- **Roll:** a kit track shows one row per slot, named after its sound, instead of piano keys. A
  strip under the roll shows velocity or one automation lane (chosen in the clip panel).
  Select + Down puts the cursor in it, Select + Up takes it back to the notes; in the strip,
  up/down changes the value at the cursor (a held run is one undo step) and B removes a point. The
  note panel lists each lockable setting with the note's lock ("-" when it follows the track), and
  a Clear locks action.
- **Mix:** a strip per track (meter, level, pan, chorus, delay and reverb sends, mute, solo) and a
  master strip (meter, level, EQ). The d-pad moves; holding A with the d-pad changes the value
  under the cursor; A switches mute and solo; B resets; X opens the effect settings.
- **Project panel** (X on an empty bar in the arrangement): tempo, key, scale, name (edited a letter
  at a time with the d-pad), Save, Load, New project, MIDI clock out, the MIDI devices found, and
  Render to WAV and Render stems. Projects go to `projects/<name>.twill`, renders to
  `renders/<name>.wav`. Stems go to `renders/<name> stems/` as `01 Drums.wav` and so on: one 32-bit
  float WAV per track the mix plays (respecting mute and solo; MIDI out tracks have none), each with
  its own share of the chorus, delay and reverb and none of the master section (EQ, compressor,
  level, limiter), all the song's length plus 3 s. They add up to the mix before the master in
  level and sound, though not sample for sample: rendered alone, a track's notes can land on voices
  whose oscillators sit at other phases, and noise-based engines draw other random numbers. Loading
  or starting anew with unsaved changes asks for a second press.
- **MIDI:** notes from any connected keyboard play the current track. "Record from MIDI" in the
  settings panel arms recording ("Rec" in the top bar): while playing, notes are recorded into the open clip, snapped to 1/16.
  MIDI clock (24 per beat) with start and stop goes out when switched on. *Polish round:* the
  engine stamps every MIDI event (notes, note-offs, clock on its own steady grid) with its sample,
  and `MidiSender`'s thread sends each when its audio reaches the speaker (two blocks after
  rendering), mapping samples to time from each callback's anchor, smoothed. Notes played live
  through a MIDI Out track go out at once. MIDI on Windows is still to come.
- *As built (2026-11-14):* **following an external clock.** The project setting "MIDI clock" is
  Off, Send or Follow (the byte that held clock-out on/off). Incoming messages carry the time they
  arrived: CoreMIDI's callback stamps them, and on Linux a reader thread waits on the raw devices
  for that (the UI thread only polls once a frame). `ClockFollower` takes the tempo from a
  least-squares line through the last 96 pulses (four beats), starting again from the last beat when
  it disagrees by more than 2%; start and continue wait for the next pulse, which is beat 0; stop
  stops; song position (while stopped) locates. The engine takes the tempo as an override (a command,
  no snapshot) and counts the ticks it has played since play; each frame the app compares that, as
  heard (less the output latency), with the master's beats and nudges the tempo up to 3% to close
  the gap, so it stays in phase rather than only near the tempo. The top bar shows "Ext 123.4 BPM".

## 8. Project model

```
Project
  name, tempo (20–300, automatable), time signature, groove, master settings, effects settings
  tracks[1–16]   (how many play depends on the device, §6)
    name, colour, voice count, mixer settings
    instrument: Plaits | Sampler | MIDI Out   (+ filter, drive, envelopes, modulators[4])
  clips (shared pool; a clip belongs to one track)
    length (ticks), loop on/off
    notes: sorted list of
      start, length (ticks)          pitch (0–127)      velocity (0–127)
      probability (0–100%), glide on/off
      parameter locks: a short list of (parameter id, value)   ← the M8 FX-command equivalent
    lanes: list of (parameter id, breakpoints[(tick, value, curve)])
  arrangement: per track, a list of (start tick, clip id, offset, length)
  markers, loop region
```

- **Parameter ids** are stable numbers per instrument type, so saved projects survive new parameters
  being added.
- **File format:** a chunked binary project file (RIFF-style chunks, each versioned) for fast
  loading on small devices, plus a JSON export/import for diffs, scripting and tests.
- **Limits** (to keep the audio side bounded): up to 256 clips, 16,384 notes per clip, 32 lanes per
  clip, 4 parameter locks per note. Generous for a handheld; easy to raise on desktop.

## 9. Build

- **CMake**, a toolchain file per target, a preset per platform (`brickpro`, `macos`, `linux-x64`,
  `headless`, later `web`).
- **Brick Pro / KNULLI:** AArch64, Cortex-A53, cross-compiled with **Zig** (`zig c++`, target
  `aarch64-linux-gnu.2.29`), which supplies glibc stubs and links libc++ statically. The binary needs
  only glibc and the device's own SDL2. *Since 2026-11-15* the target is glibc 2.29, PortMaster's
  floor (the binary itself asks for no more than 2.27); the KNULLI SDL2 it links against was built
  for a newer glibc, so the link skips checking that library's own references
  (`--allow-shlib-undefined`), which the device resolves. The Cortex-A53 tuning stays: the binary
  has only ARMv8.0 instructions, so any 64-bit handheld runs it, and it's about 4% faster on the
  Brick than generic ARMv8-A. A small sysroot holds that SDL2 and matching headers
  (`tools/make-knulli-sysroot.sh`). On the device SDL2 uses the `mali` video driver with the
  OpenGL ES 2 renderer, and ALSA (PipeWire) audio.
- **Packaging:** a launcher script and an app folder for KNULLI's Ports, as with
  LittleGPTracker; and a PortMaster package (*2026-11-15*): `packaging/portmaster/` holds the
  launcher (PortMaster's standard template: its controller mapping, gptokeyb2 for the close-app
  shortcut only, no bundled libraries), `port.json` (rtr, `!lowres`, `min_glibc` 2.27), the README
  and gameinfo; `tools/make-portmaster-zip.sh` adds the stripped binary as `twill.aarch64` and the
  licences from `third_party/` and writes a zip in the installed layout. Checked at PortMaster's
  test sizes with headless renders: 640x480, 720x720 (the top bar drops the track name, the master
  strip uses short EQ labels) and 1280x720 are fine; 480x320 works but is cramped, hence `!lowres`.
  On a GameController pad a stick click stands in for Menu (not every handheld has one).
- **Third-party code** is vendored under `third_party/` with its licences and source commit: `plaits/dsp`,
  the `stmlib` subset, Clouds' reverb, `dr_wav`, and the font (OFL).

## 10. Testing

- **Headless renders:** fixture projects render to WAV and are compared with golden files within a
  tolerance, catching timing and instrument regressions.
- **Unit tests:** scheduler timing (notes land on the right sample at every tempo and groove), the
  snapshot hand-off between threads, file round trips (including every older file version), the
  resampler.
- **UI tests:** scripted button presses on the headless platform, checking the project model and the
  rendered frames (PNG).
- **Benchmarks:** every Plaits engine, the sampler, modulators and effects, timed per block on each
  target.

## 11. Milestones

| | | Proves |
|---|---|---|
| M0 | Toolchain and a "hello" app on the Brick Pro: native-resolution window, the rasterizer and a font, a tone, buttons moving a cursor, launched from Ports | cross-compiling, packaging, drawing speed |
| M1 | Plaits in the engine, headless: every engine rendered to WAV on the Mac and benchmarked on the Brick Pro | CPU budget and voice counts; Plaits builds unmodified |
| M2 | Project model, scheduler, snapshot hand-off, file format; headless golden tests | timing and the threading model |
| M3 | Piano roll and arrangement on SDL2 (Mac for development, Brick Pro for real use), with the button grammar | that a gamepad piano roll is pleasant; iterate here |
| M4 | Instrument view, Sampler (slices, multi-sample kits), browser | both instrument families |
| M5 | Modulators, automation lanes, parameter locks, mixer and effects, MIDI in/out | M8-level depth |
| M6 | A second platform (web or another handheld) with no core changes | the portability claim |

M3 is the riskiest: the button grammar in §7 is a starting point, and it needs real time on the device.

**Status:** M0–M5 done (2026-11-11/12): arrangement, piano roll, sound, mixer; macro-oscillator
and sampler instruments with modulators, automation lanes and parameter locks; send effects; project
files; MIDI. Each section above has an "as built" note where the build differs from or settles the
plan. Next is M6.

### Song names (2026-11-14)

New projects get a made-up two-word name, an adjective-like word and a thing or place ("Velvet
Harbour", "Folded Voyage"), from two lists of our own (`app/SongNames.cpp`: 183 x 154 words, about 28,000 names, in three
moods: weather and materials, empty buildings after hours, and rainbows and old sound chips), one
no saved project already has. "New song names: Made up / Untitled" in the settings panel turns it
off (an app setting, kept in settings.txt). X in the name editor makes up another.

## 12. Decisions so far, and open questions

Decided (2026-11-11):

- **Name:** Twill Studio (decided 2026-11-14; Fluorescent was the placeholder). Where the full
  name won't fit, just **Twill**. The program is
  `twill`, its KNULLI folder `roms/ports/twill/` with the launcher "Twill Studio.sh", and projects
  are `.twill` files tagged "TWIL". Files from before, tagged "FLUO" (with the old `.fluo`
  extension), still load.
- **Track count** is set per platform (§6).
- **Polyphony:** chords per track (1–8 voices per track, §6).
- **Resolution:** always the highest the device offers (§7).
- **Parameter locks:** yes, from the start.
- **Instruments:** Plaits, Sampler and MIDI Out. No further instrument types planned for now.
- **No name for the synth family (2026-11-12):** Engine is one list (the 24 synth engines by
  descriptive names, then Sampler, then MIDI out), so the app never needs one. "Plaits" and
  "Mutable Instruments" stay out of everything user-facing; credit goes in the README.
- **Live/clip-launching mode:** after version 1. The engine should still be able to switch a track's
  clip on a bar boundary, so it can be added without restructuring.
- **Targets:** the Brick Pro and desktop (macOS, Linux, Windows) first; then other Linux handhelds,
  web and phones/tablets. Not embedded targets such as Daisy, and not a DAW plugin.
- **Hosting:** a private repository on the NAS for now, kept so that it could be open-sourced later.

Open:

1. **License** for an eventual open-source release. Plaits and stmlib are MIT, so any choice works;
   note that GPL is a problem for the iOS App Store.
