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
third_party/  plaits/, stmlib/, clouds reverb (MIT), dr_wav (public domain)
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
  The track settings panel (X on a track's name) has **Position** (Left/Right moves the track one
  place, R + Left/Right to the top or the end) and **Clear track** (no clips, default sound, named
  for its place). Both are single undo steps. A track's colour is its own (an index into the
  theme's set, saved in the file), so it keeps it when moved.

### Voices and polyphony

- Each track has **1–8 voices** (default 1 for Plaits, 4 for Sampler), with oldest-note stealing.
  Mono tracks get legato and glide, as on the M8.
- *As built:* in a kit, Voices counts per drum (per note): drums never cut each other off, even at
  one voice, while a drum hit again cuts off its own last hit. The row reads "Voices per drum".
- *As built:* on a one-voice macro track every note retriggers, including a note that starts
  exactly where the last one ended. A note with **glide** slurs into the next pitch with no
  retrigger. **Voices: Legato** (one step below 1; stored as 0) slurs every overlapping or touching
  note without sliding. In ADSR mode the low-pass gate stays open and the ADSR is a plain amplifier
  after it; a retrigger restarts the attack from zero, ramping down to it over one grid block.
- *As built (device):* the whole engine averages about 37% of one Brick Pro core on the demo song
  with effects. Plaits itself is about 2% per voice; that song keeps about 14 voices sounding.
  Handhelds use 512-frame audio blocks on a SCHED_FIFO thread: 256 frames underran.
- *As built (device):* the Brick Pro's codec compresses its output in hardware (the device tree's
  `dacdrc_cfg`; bit 29 of DAC_DAP_CTL), which made beats pump. While audio is open the SDL2
  platform turns that bit off (`OutputCompressor`, Linux, this codec only) and restores it on close.
- **Plaits is monophonic per voice**, so a 4-voice Plaits track runs four `plaits::Voice` instances.
  Its chord engine is still there for one-voice chords.
- **CPU budget (measured in M1 with `twill_macro_lab bench`, 256-frame blocks).** On the Brick
  Pro (Cortex-A53 at 1.6 GHz) one macro-oscillator voice costs 0.6–2.7% of one core on average,
  depending on the engine (Chiptune and Speech cheapest; Formant, Particles and Plucked string
  dearest), with worst-case blocks up to 5.2% (Swarm). That is about 36 voices per core for the
  heaviest engines. The Brick Pro's limit is set at **16 voices** for now, under half of one core
  in the worst case, leaving room for samplers, effects and the UI. On an Apple-silicon Mac a voice
  costs at most 0.5% of a core.
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
  - **ADSR:** our amp envelope drives `level`, so note length and release behave like a normal synth.

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
- **Factory sounds:** on first launch the app renders sixteen sounds of its own from the macro
  oscillator (kicks, snares, hats, toms, rim, noise, bell, particles, zap, and pluck, keys and pad
  at C3) into `samples/Factory/`, and a kit fills with them in one step. No third-party samples.

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
  separate reader was needed); converted to stereo float at 48 kHz on load (Hermite resampling).
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

- **gfx/** is a small software rasterizer of our own: filled and rounded rectangles, lines, alpha
  blending, clipping, bitmap blits. It needs no GPU, so it ports anywhere a framebuffer can be shown.
- **Fonts** are pre-rendered at build time from an open-licence font (one sans, one mono; OFL) into
  antialiased bitmap atlases at several pixel sizes. There's no FreeType or font file parsing on the
  device.
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
- **Themes:** a small set of colour tokens (background, grid lines, bar lines, scale rows, note fill
  per track, selection, playhead). Light is the default. *As built:* a dark theme (graphite, the
  same design; piano keys stay light) is the first row of the Settings panel (Menu). It is an app
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
change a value), mapped onto a handheld's buttons. L and R mean either shoulder button on their
side: L2 and R2 do exactly what L1 and R1 do (2026-11-14), so whichever is easier to reach works.

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
| L / R (tap) | previous / next track |
| L / R held + d-pad | in the roll: the grid (left/right; the view zooms with it, and a message names the new grid) and scroll by page (up/down); in the arrangement: zoom |
| Select held + d-pad | switch view: arrangement, piano roll, instrument, mixer |
| Start | play / stop (Select + Start: play from the cursor) |

Most handhelds also have L2/R2 and analog sticks; they add shortcuts (sticks for scrolling, L2/R2 for
snap and velocity) but nothing requires them. On desktop, a mouse and keyboard work as in any piano
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
  Select + Start plays from the cursor. **Menu + Start quits**, since Select + Start is taken. **Menu on its own opens the
  project panel** (tempo, key, scale, files, MIDI clock) from any view; the hint bar says so when
  there is room. (X on an empty bar in the arrangement still opens it too.)
  (Select + up/down changed tempo in M3; since M5 it moves between the notes and the lane strip, and
  tempo is in the project panel.)
- **Start** plays the song in the arrangement and loops the edited clip's placement in the roll.
- **X** opens a side panel: the selected notes (velocity, probability, glide, length), or with
  nothing selected the clip and grid (grid, key, scale, fold to scale, clip length).
- The hint bar at the bottom changes with the buttons held, so the grammar teaches itself.

**Arrangement** (M3): the d-pad moves by bar and track (left of bar 1 is the track header). A on
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
  Render to WAV. Projects go to `projects/<name>.fluo`, renders to `renders/<name>.wav`. Loading or
  starting anew with unsaved changes asks for a second press.
- **MIDI:** notes from any connected keyboard play the current track. Select + X arms recording
  ("Rec" in the top bar): while playing, notes are recorded into the open clip, snapped to 1/16.
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
  `aarch64-linux-gnu.2.38`), which supplies glibc stubs and links libc++ statically. The binary needs
  only glibc and the device's own SDL2. glibc 2.38 is the oldest the device's SDL2 links against
  (KNULLI 20260921 has 2.40). A small sysroot holds that SDL2 and matching headers
  (`tools/make-knulli-sysroot.sh`). On the device SDL2 uses the `mali` video driver with the
  OpenGL ES 2 renderer, and ALSA (PipeWire) audio.
- **Packaging:** a launcher script and an app folder for KNULLI's Ports, as with
  LittleGPTracker; a PortMaster package later.
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

**Status:** M0–M5 done (2026-11-11/12). Each section above has an "as built" note where the build
differs from or settles the plan. Next is M6.

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
