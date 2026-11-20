// Headless tests (design §10): scheduler timing, the snapshot hand-off between threads, project
// files, undo, and golden renders. No display or sound device needed, so they also run on a
// handheld over SSH.
//
//   twill_tests [--golden-dir DIR] [--update-golden] [name-filter]
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "app/App.h"
#include "app/FactorySounds.h"
#include "app/ClockFollower.h"
#include "app/SongNames.h"
#include "app/MidiSender.h"
#include "engine/Effects.h"
#include "engine/Engine.h"
#include "engine/PlaitsVoice.h"
#include "engine/Sample.h"
#include "model/Edit.h"
#include "model/ProjectFile.h"
#include "platform/null/NullPlatform.h"

namespace {

int g_failures = 0;
std::string g_golden_dir =
#ifdef TWILL_GOLDEN_DIR
    TWILL_GOLDEN_DIR;
#else
    "tests/golden";
#endif
bool g_update_golden = false;

#define CHECK(cond)                                                              \
  do {                                                                           \
    if (!(cond)) {                                                               \
      std::printf("    FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      ++g_failures;                                                              \
    }                                                                            \
  } while (0)

#define CHECK_MSG(cond, ...)                                                     \
  do {                                                                           \
    if (!(cond)) {                                                               \
      std::printf("    FAILED %s:%d: %s: ", __FILE__, __LINE__, #cond);          \
      std::printf(__VA_ARGS__);                                                  \
      std::printf("\n");                                                         \
      ++g_failures;                                                              \
    }                                                                            \
  } while (0)

using twill::Tick;
constexpr Tick Q = twill::kTicksPerQuarter;
constexpr int kRate = twill::Engine::kSampleRate;

// A project with `tracks` macro tracks, each with one clip of `clip_len` placed from 0 for
// `placed_len`.
twill::Project make_project(int tracks = 1, Tick clip_len = 16 * Q, Tick placed_len = 16 * Q) {
  twill::Project p;
  p.loop = false;
  for (int i = 0; i < tracks; ++i) {
    twill::Track t;
    t.name = "Track " + std::to_string(i + 1);
    p.tracks.push_back(t);
    twill::Clip& c = p.add_clip(i, clip_len);
    p.tracks[i].arrangement.push_back(twill::Placement{0, c.id, 0, placed_len});
  }
  return p;
}

twill::Note note_at(Tick start, uint8_t pitch = 60, Tick length = Q / 2) {
  twill::Note n;
  n.start = start;
  n.pitch = pitch;
  n.length = length;
  return n;
}

struct Recorder : twill::Engine::Observer {
  struct On {
    int track;
    uint8_t pitch;
    uint64_t sample;
    twill::MacroParams params;
  };
  std::vector<On> ons;
  void note_on(int track, uint8_t pitch, uint64_t sample, const twill::MacroParams& p) override {
    ons.push_back(On{track, pitch, sample, p});
  }
};

// Renders `seconds` of a project from the start, in callbacks of `block` frames (256 is the
// Brick Pro's).
std::vector<float> render(const twill::Project& p, float seconds, Recorder* rec = nullptr,
                          int block = 256) {
  twill::Engine e(16);
  e.set_observer(rec);
  e.submit(twill::build_snapshot(p, 16, 1));
  e.play();
  std::vector<float> out(size_t(seconds * kRate) * 2);
  for (size_t pos = 0; pos < out.size() / 2; pos += size_t(block)) {
    int n = int(std::min<size_t>(size_t(block), out.size() / 2 - pos));
    e.render(&out[pos * 2], n);
  }
  return out;
}

// The scheduler starts a note at the start of the 12-sample sub-block that contains its exact
// time, so the reported sample is at most 12 samples early.
bool near_exact(uint64_t sample, double exact) {
  return double(sample) <= exact + 1e-6 && double(sample) > exact - twill::Engine::kSubBlock;
}

// --- Tests --------------------------------------------------------------------------------

void test_timing_at_120() {
  twill::Project p = make_project();
  twill::Clip& c = p.clips[0];
  c.notes = {note_at(0), note_at(Q), note_at(2 * Q + Q / 2)};
  Recorder rec;
  render(p, 2.0f, &rec);
  CHECK(rec.ons.size() == 3);
  if (rec.ons.size() == 3) {
    CHECK(rec.ons[0].sample == 0);
    CHECK(rec.ons[1].sample == 24000);  // one beat at 120 BPM
    CHECK(rec.ons[2].sample == 60000);  // two and a half beats
  }
}

void test_timing_at_odd_tempo() {
  twill::Project p = make_project();
  p.tempo = 137.5f;
  p.clips[0].notes = {note_at(3 * Q), note_at(5 * Q + 7)};
  Recorder rec;
  render(p, 3.0f, &rec);
  CHECK(rec.ons.size() == 2);
  if (rec.ons.size() == 2) {
    double spb = 60.0 / 137.5 * kRate;  // samples per beat
    CHECK_MSG(near_exact(rec.ons[0].sample, 3 * spb), "got %llu, want ~%.1f",
              (unsigned long long)rec.ons[0].sample, 3 * spb);
    CHECK_MSG(near_exact(rec.ons[1].sample, (5 + 7.0 / Q) * spb), "got %llu, want ~%.1f",
              (unsigned long long)rec.ons[1].sample, (5 + 7.0 / Q) * spb);
  }
}

void test_loop_region() {
  twill::Project p = make_project();
  p.loop = true;
  p.loop_start = 0;
  p.loop_end = 4 * Q;  // one bar
  p.clips[0].notes = {note_at(0), note_at(3 * Q)};
  Recorder rec;
  render(p, 4.5f, &rec);  // two and a quarter bars
  std::vector<uint64_t> want = {0, 72000, 96000, 168000, 192000};
  CHECK(rec.ons.size() == want.size());
  for (size_t i = 0; i < std::min(want.size(), rec.ons.size()); ++i)
    CHECK_MSG(near_exact(rec.ons[i].sample, double(want[i])), "note %zu at %llu, want %llu", i,
              (unsigned long long)rec.ons[i].sample, (unsigned long long)want[i]);
}

void test_placement_loops_its_clip() {
  // A half-bar clip placed at bar 2 for two bars: plays four times.
  twill::Project p = make_project(1, 2 * Q, 0);
  p.tracks[0].arrangement = {twill::Placement{4 * Q, p.clips[0].id, 0, 8 * Q}};
  p.clips[0].notes = {note_at(0, 48)};
  Recorder rec;
  render(p, 7.0f, &rec);
  CHECK(rec.ons.size() == 4);
  for (size_t i = 0; i < rec.ons.size(); ++i)
    CHECK(near_exact(rec.ons[i].sample, 96000.0 + i * 48000.0));
}

void test_placement_offset() {
  // Starting one beat into the clip skips the note on beat 1.
  twill::Project p = make_project(1, 4 * Q, 0);
  p.tracks[0].arrangement = {twill::Placement{0, p.clips[0].id, Q, 3 * Q}};
  p.clips[0].notes = {note_at(0, 60), note_at(2 * Q, 62)};
  Recorder rec;
  render(p, 2.0f, &rec);
  CHECK(rec.ons.size() == 1);
  if (!rec.ons.empty()) {
    CHECK(rec.ons[0].pitch == 62);
    CHECK(near_exact(rec.ons[0].sample, 24000));
  }
}

void test_probability() {
  twill::Project p = make_project(3, 100 * (Q / 4), 100 * (Q / 4));
  for (int t = 0; t < 3; ++t)
    for (int i = 0; i < 100; ++i) {
      twill::Note n = note_at(i * (Q / 4), uint8_t(60 + t), Q / 8);
      n.probability = t == 0 ? 0 : t == 1 ? 100 : 50;
      p.clips[size_t(t)].notes.push_back(n);
    }
  Recorder rec;
  render(p, 13.0f, &rec);
  int count[3] = {};
  for (auto& on : rec.ons) ++count[on.track];
  CHECK(count[0] == 0);
  CHECK(count[1] == 100);
  CHECK_MSG(count[2] > 30 && count[2] < 70, "50%% notes played %d of 100", count[2]);
}

void test_parameter_locks() {
  twill::Project p = make_project();
  p.tracks[0].macro.timbre = 0.25f;
  twill::Note locked = note_at(0);
  locked.set_lock(twill::kParamTimbre, 0.9f);
  locked.set_lock(twill::kParamEngine, 13);
  p.clips[0].notes = {locked, note_at(Q)};
  Recorder rec;
  render(p, 1.0f, &rec);
  CHECK(rec.ons.size() == 2);
  if (rec.ons.size() == 2) {
    CHECK(rec.ons[0].params.timbre == 0.9f);
    CHECK(rec.ons[0].params.engine == 13);
    CHECK(rec.ons[1].params.timbre == 0.25f);  // the lock belongs to its note only
    CHECK(rec.ons[1].params.engine == p.tracks[0].macro.engine);
  }
}

void test_mute_and_solo() {
  twill::Project p = make_project(3);
  for (int t = 0; t < 3; ++t) p.clips[size_t(t)].notes = {note_at(0, uint8_t(60 + t))};
  p.tracks[0].mute = true;
  Recorder rec;
  render(p, 0.5f, &rec);
  CHECK(rec.ons.size() == 2);
  p.tracks[0].mute = false;
  p.tracks[2].solo = true;
  Recorder rec2;
  render(p, 0.5f, &rec2);
  CHECK(rec2.ons.size() == 1 && rec2.ons[0].track == 2);
}

void test_track_limit() {
  // A device that plays two tracks keeps the third in the project but doesn't play it.
  twill::Project p = make_project(3);
  for (int t = 0; t < 3; ++t) p.clips[size_t(t)].notes = {note_at(0)};
  auto snap = twill::build_snapshot(p, 2, 1);
  CHECK(snap->tracks.size() == 2);
  CHECK(p.tracks.size() == 3);
}

void test_sound_is_produced() {
  twill::Project p = make_project();
  p.clips[0].notes = {note_at(0, 48, Q)};
  std::vector<float> out = render(p, 1.0f);
  float peak = 0;
  for (float s : out) peak = std::max(peak, std::fabs(s));
  CHECK_MSG(peak > 0.05f, "peak %.4f", peak);
  CHECK(peak <= 1.0f);
}

twill::Project rich_project() {
  twill::Project p = make_project(2, 8 * Q, 16 * Q);
  p.name = "Round trip";
  p.tempo = 97.25f;
  p.loop = true;
  p.loop_start = 2 * Q;
  p.loop_end = 14 * Q;
  p.tracks[0].macro.engine = 14;
  p.tracks[0].macro.timbre = 0.33f;
  p.tracks[0].macro.output = twill::MacroOutput::Mix;
  p.tracks[0].voices = 4;
  p.tracks[0].pan = -0.4f;
  p.tracks[1].mute = true;
  p.tracks[1].colour = 3;
  p.key = 7;
  p.scale = twill::ScaleType::Dorian;
  p.tracks[1].instrument = twill::InstrumentType::Sampler;
  p.tracks[1].sampler.mode = twill::SamplerMode::Kit;
  p.tracks[1].sampler.sample = "samples/loop.wav";
  p.tracks[1].sampler.loop = twill::LoopMode::PingPong;
  p.tracks[1].sampler.slice_transients = true;
  p.tracks[1].sampler.kit[3] = twill::KitSlot{"/abs/snare.aif", -2, 0.5f, 0.25f, 2, 0.4f};
  p.tracks[1].tone.filter = twill::FilterType::BandPass;
  p.tracks[1].tone.cutoff = 0.4f;
  p.tracks[0].send_reverb = 0.6f;
  p.mix.reverb_size = 0.9f;
  p.midi_clock = twill::MidiClock::Send;
  p.tracks[1].midi_channel = 9;
  p.tracks[1].midi_cc[0] = 11;
  p.tracks[1].midi_cc[1] = 71;
  p.mix.eq_high = -0.25f;
  p.mix.comp_threshold = 0.5f;
  p.mix.comp_ratio = 0.7f;
  p.mix.comp_release = 0.2f;
  twill::Lane lane;
  lane.param = twill::kParamCutoff;
  lane.set_point(0, 0.2f);
  lane.set_point(4 * Q, 0.9f);
  p.clips[0].lanes.push_back(lane);
  p.tracks[0].mods[1] = twill::Modulator{twill::ModType::Lfo, twill::kParamTimbre, -0.3f, twill::LfoShape::Square, 0.7f, true, 0.1f, 0.2f, false};
  twill::Note n = note_at(Q / 3, 67, Q * 2);
  n.velocity = 90;
  n.probability = 75;
  n.glide = true;
  n.set_lock(twill::kParamMorph, 0.125f);
  twill::EditHistory h(p);
  h.add_note(p.clips[0].id, n);
  h.add_note(p.clips[1].id, note_at(Q, 40));
  p.tracks[1].arrangement.push_back(twill::Placement{8 * Q, p.clips[1].id, Q, 4 * Q});
  return p;
}

void test_file_round_trip() {
  twill::Project p = rich_project();
  std::vector<uint8_t> bytes = twill::save_project(p);
  twill::Project q;
  std::string err;
  CHECK(twill::load_project(bytes.data(), bytes.size(), q, err));
  CHECK_MSG(q == p, "loaded project differs (%s)", err.c_str());
  // Saved before the app was renamed: the old tag, the same format.
  CHECK(bytes.size() > 4 && std::memcmp(bytes.data(), "TWIL", 4) == 0);
  std::memcpy(bytes.data(), "FLUO", 4);
  twill::Project old;
  CHECK(twill::load_project(bytes.data(), bytes.size(), old, err) && old == p);
}

void test_file_skips_unknown_data() {
  // A file from a "newer version": an unknown chunk, and extra fields at the end of PROJ.
  twill::Project p = rich_project();
  std::vector<uint8_t> bytes = twill::save_project(p);
  // Find PROJ, grow it by 6 bytes of future fields.
  size_t proj = 8;
  CHECK(std::memcmp(&bytes[proj], "PROJ", 4) == 0);
  uint32_t len;
  std::memcpy(&len, &bytes[proj + 4], 4);
  const uint8_t extra[6] = {1, 2, 3, 4, 5, 6};
  bytes.insert(bytes.begin() + long(proj + 8 + len), extra, extra + 6);
  len += 6;
  std::memcpy(&bytes[proj + 4], &len, 4);
  const uint8_t chunk[] = {'Z', 'Z', 'Z', 'Z', 3, 0, 0, 0, 9, 9, 9};
  bytes.insert(bytes.end(), chunk, chunk + sizeof chunk);

  twill::Project q;
  std::string err;
  CHECK_MSG(twill::load_project(bytes.data(), bytes.size(), q, err), "%s", err.c_str());
  CHECK(q == p);
}

void test_file_rejects_damage() {
  twill::Project p = rich_project();
  std::vector<uint8_t> bytes = twill::save_project(p);
  twill::Project q;
  std::string err;
  CHECK(!twill::load_project(bytes.data(), bytes.size() / 2, q, err));  // truncated
  std::vector<uint8_t> junk(200, 0x5a);
  CHECK(!twill::load_project(junk.data(), junk.size(), q, err));
  bytes[4] = 99;  // a format version from the future
  CHECK(!twill::load_project(bytes.data(), bytes.size(), q, err));
}

void test_undo_redo() {
  twill::Project p = make_project();
  twill::EditHistory h(p);
  const twill::Project empty = p;
  uint32_t clip = p.clips[0].id;

  uint32_t id = h.add_note(clip, note_at(0, 60));
  CHECK(id != 0 && p.clips[0].notes.size() == 1);
  twill::Note moved = *p.clips[0].find(id);
  moved.start = Q;
  moved.pitch = 64;
  CHECK(h.change_note(clip, moved));
  for (float v : {0.1f, 0.2f, 0.3f}) h.set_track_param(0, twill::kParamTimbre, v);  // one step
  const twill::Project edited = p;

  CHECK(h.undo());  // the three timbre changes
  CHECK(p.tracks[0].macro.timbre == empty.tracks[0].macro.timbre);
  CHECK(h.undo());  // the move
  CHECK(p.clips[0].notes[0].start == 0 && p.clips[0].notes[0].pitch == 60);
  CHECK(h.undo());  // the add
  CHECK(p.clips[0].notes.empty());
  CHECK(!h.undo());
  while (h.redo()) {
  }
  CHECK(p == edited);

  // A group undoes as one step.
  h.begin_group();
  h.add_note(clip, note_at(2 * Q, 67));
  h.add_note(clip, note_at(3 * Q, 69));
  h.end_group();
  CHECK(p.clips[0].notes.size() == 3);
  CHECK(h.undo());
  // Same notes and tracks. (The clip's next note id stays advanced: ids are never reused.)
  CHECK(p.clips[0].notes == edited.clips[0].notes && p.tracks == edited.tracks);
}

void test_snapshot_handoff_under_load() {
  // The UI thread edits and submits snapshots as fast as it can while the audio thread renders.
#ifdef __EMSCRIPTEN__
  return;  // the web build has one thread, so there is no hand-off to race
#endif
  twill::Project p = make_project();
  twill::EditHistory h(p);
  twill::Engine e(16);
  e.submit(twill::build_snapshot(p, 16, h.revision()));
  e.play();
  std::atomic<bool> run{true};
  std::thread audio([&] {
    std::vector<float> buf(512);
    while (run.load()) e.render(buf.data(), 256);
  });
  for (int i = 0; i < 3000; ++i) {
    h.add_note(p.clips[0].id, note_at(Tick((i * 37) % (16 * Q)), uint8_t(48 + i % 24), Q / 8));
    if (i % 3 == 0) h.undo();
    e.submit(twill::build_snapshot(p, 16, h.revision()));
  }
  // Wait for the audio thread to pick up the last one.
  for (int i = 0; i < 2000 && e.playing_revision() != h.revision(); ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  CHECK(e.playing_revision() == h.revision());
  run.store(false);
  audio.join();
  e.collect_garbage();
}

twill::Project golden_project();

void test_same_output_at_any_block_size() {
  // Devices call back with different block sizes; the music must not depend on it.
  std::vector<float> a = render(golden_project(), 2.0f, nullptr, 256);
  for (int block : {1, 7, 441, 1024}) {
    std::vector<float> b = render(golden_project(), 2.0f, nullptr, block);
    CHECK_MSG(a == b, "block size %d renders differently", block);
  }
}

// The fixture for golden renders: a bass line, chords with locks, and a gliding lead.
twill::Project golden_project() {
  twill::Project p = make_project(3, 8 * Q, 16 * Q);
  p.tempo = 126;
  p.tracks[0].macro.engine = 8;  // virtual analog bass
  p.tracks[0].macro.decay = 0.4f;
  p.tracks[1].macro.engine = 13;  // wavetable chords
  p.tracks[1].voices = 3;
  p.tracks[1].level = 0.5f;
  p.tracks[1].pan = 0.3f;
  p.tracks[2].macro.engine = 11;  // formant lead
  p.tracks[2].pan = -0.3f;
  const uint8_t bass[] = {36, 36, 43, 36, 39, 36, 41, 43};
  for (int i = 0; i < 8; ++i) p.clips[0].notes.push_back(note_at(i * Q, bass[i], Q / 2));
  for (int bar = 0; bar < 2; ++bar)
    for (uint8_t pitch : {60, 63, 67}) {
      twill::Note n = note_at(bar * 4 * Q, uint8_t(pitch + bar * 5), 3 * Q);
      if (bar == 1) n.set_lock(twill::kParamTimbre, 0.8f);
      p.clips[1].notes.push_back(n);
    }
  const uint8_t lead[] = {72, 75, 79, 77};
  for (int i = 0; i < 4; ++i) {
    twill::Note n = note_at(i * 2 * Q, lead[i], 2 * Q);
    n.glide = i > 0;
    p.clips[2].notes.push_back(n);
  }
  for (twill::Clip& c : p.clips) {
    uint32_t id = 1;
    for (twill::Note& n : c.notes) n.id = id++;
    c.next_note_id = id;
    c.sort();
  }
  return p;
}

bool write_wav(const std::string& path, const std::vector<float>& stereo) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
  uint32_t bytes = uint32_t(stereo.size() * 2);
  std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
  u32(16); u16(1); u16(2); u32(kRate); u32(kRate * 4); u16(4); u16(16);
  std::fwrite("data", 1, 4, f); u32(bytes);
  for (float s : stereo) u16(uint16_t(int16_t(std::lround(std::max(-1.0f, std::min(1.0f, s)) * 32767))));
  return std::fclose(f) == 0;
}

bool read_wav(const std::string& path, std::vector<float>& stereo) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::vector<uint8_t> d;
  uint8_t buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
  std::fclose(f);
  if (d.size() < 44) return false;
  stereo.resize((d.size() - 44) / 2);
  for (size_t i = 0; i < stereo.size(); ++i) {
    int16_t v;
    std::memcpy(&v, &d[44 + 2 * i], 2);
    stereo[i] = v / 32767.0f;
  }
  return true;
}

// Four touching notes on a one-voice macro track (each ends where the next begins). Returns the
// loudness just after the 2nd, 3rd and 4th notes begin, relative to just before.
std::vector<double> touching_note_jumps(int voices, int amp_mode, float attack,
                                        const twill::Modulator* mod = nullptr) {
  twill::Project p;
  p.tempo = 112;
  twill::Track t;
  t.macro.engine = 11;
  t.voices = voices;
  t.level = 1;
  t.tone.amp_mode = amp_mode;
  t.tone.attack = attack;
  if (mod) {  // an envelope on a low cutoff: each restart is a bright, loud burst
    t.tone.filter = twill::FilterType::LowPass;
    t.tone.cutoff = 0.15f;
    t.mods[0] = *mod;
  }
  p.tracks.push_back(t);
  const Tick S = twill::kTicksPerQuarter / 4;
  twill::Clip& c = p.add_clip(0, 16 * S);
  const int pitches[4] = {67, 70, 74, 77};
  for (int i = 0; i < 4; ++i) {
    twill::Note n;
    n.id = c.next_note_id++;
    n.start = i * 4 * S;
    n.length = 4 * S;
    n.pitch = uint8_t(pitches[i]);
    c.notes.push_back(n);
  }
  p.tracks[0].arrangement.push_back(twill::Placement{0, c.id, 0, 16 * S});
  twill::Engine e(16);
  e.submit(twill::build_snapshot(p, 8, 1));
  e.play();
  std::vector<float> out(2 * 96000);
  for (size_t pos = 0; pos < out.size() / 2; pos += 256) e.render(&out[pos * 2], 256);
  auto rms = [&](long from, int len) {
    double sum = 0;
    for (int i = 0; i < len; ++i) sum += double(out[2 * size_t(from + i)]) * out[2 * size_t(from + i)];
    return std::sqrt(sum / len);
  };
  const double samples_per_s = 60.0 / 112 / 4 * 48000;
  std::vector<double> jumps;
  for (int b = 1; b < 4; ++b) {
    const long at = long(b * 4 * samples_per_s);
    jumps.push_back(rms(at + 480, 480) / std::max(1e-9, rms(at - 960, 480)));
  }
  return jumps;
}

void test_touching_notes_retrigger_unless_legato() {
  // The low-pass gate rings down through each note; every new note strikes it again.
  for (double j : touching_note_jumps(1, 0, 0)) CHECK(j > 3);
  // Legato: no new strike, the ringing just carries on.
  for (double j : touching_note_jumps(0, 0, 0)) CHECK(j < 1.5);
  // ADSR with a slow attack (63 ms): each note starts quiet again, 10-20 ms in.
  for (double j : touching_note_jumps(1, 1, 0.45f)) CHECK(j < 0.8);
  for (double j : touching_note_jumps(0, 1, 0.45f)) CHECK(j > 0.8);
}

void test_legato_keeps_modulators() {
  // An envelope modulator opening the filter: it restarts with every struck note, but a legato
  // note carries it on, like the amp envelope.
  twill::Modulator env;
  env.type = twill::ModType::Envelope;
  env.dest = twill::kParamCutoff;
  env.depth = 0.8f;
  env.decay = 0.45f;  // about 60 ms
  for (double j : touching_note_jumps(1, 1, 0, &env)) CHECK_MSG(j > 2, "struck: %.2f", j);
  for (double j : touching_note_jumps(0, 1, 0, &env)) CHECK_MSG(j < 1.4, "legato: %.2f", j);
}

// Out of voices: a chord bigger than the engine's voice pool cuts held notes off, and says so.
void test_voices_stolen_counted() {
  for (int pool : {8, 2}) {
    twill::Project p;
    twill::Track t;
    t.voices = 4;
    t.level = 1;
    p.tracks.push_back(t);
    twill::Clip& c = p.add_clip(0, 4 * Q);
    for (int i = 0; i < 4; ++i) {
      twill::Note n;
      n.id = c.next_note_id++;
      n.start = 0;
      n.length = 4 * Q;
      n.pitch = uint8_t(60 + 4 * i);
      c.notes.push_back(n);
    }
    p.tracks[0].arrangement.push_back(twill::Placement{0, c.id, 0, 4 * Q});
    twill::Engine e(pool);
    e.submit(twill::build_snapshot(p, 8, 1));
    e.play();
    std::vector<float> buf(512);
    for (int b = 0; b < 20; ++b) e.render(buf.data(), 256);
    CHECK(pool == 8 ? e.voices_stolen() == 0 : e.voices_stolen() == 2);
  }
}

// The master compressor at -20 dB, 4:1 (7.5 dB makeup): off leaves the signal alone; a steady
// -6 dB tone settles at -9 dB; one below the threshold just gets the makeup.
void test_master_compressor() {
  auto level_db = [](float amplitude, float threshold, float ratio) {
    twill::Effects fx;
    twill::MixSettings m;
    m.comp_threshold = threshold;
    m.comp_ratio = ratio;
    std::vector<float> buf(2 * 256);
    float peak = 0;
    for (int b = 0; b < 200; ++b) {  // about a second of a 375 Hz sine
      for (int i = 0; i < 256; ++i)
        buf[size_t(2 * i)] = buf[size_t(2 * i + 1)] = amplitude * std::sin(6.2831853f * float((b * 256 + i) % 128) / 128);
      fx.master_compressor(m, buf.data(), 256);
      if (b >= 190)
        for (float x : buf) peak = std::max(peak, std::fabs(x));
    }
    return 20 * std::log10(peak);
  };
  const float loud = std::pow(10.0f, -6.0f / 20), quiet = std::pow(10.0f, -30.0f / 20);
  const float ratio4 = (4.0f - 1.5f) / 8.5f;
  CHECK(std::fabs(level_db(loud, 0, ratio4) - -6) < 0.01f);  // off
  const float on = level_db(loud, 0.5f, ratio4);
  CHECK_MSG(std::fabs(on - -9) < 0.5f, "compressed to %.2f dB", on);
  const float under = level_db(quiet, 0.5f, ratio4);
  CHECK_MSG(std::fabs(under - (-30 + 7.5f)) < 0.3f, "under the threshold: %.2f dB", under);
}

// The master limiter: quiet audio passes untouched (only delayed); loud audio never goes over
// the ceiling; off, it's a soft clip.
void test_master_limiter() {
  const int n = 48000;
  std::vector<float> in(size_t(2 * n));
  for (int i = 0; i < n; ++i) {
    const float amp = i < n / 2 ? 0.5f : 3.0f;  // half a second quiet, then far too loud
    in[size_t(2 * i)] = in[size_t(2 * i + 1)] = amp * std::sin(6.2831853f * 220 * i / kRate);
  }
  for (bool on : {true, false}) {
    twill::Effects fx;
    twill::MixSettings m;
    m.limiter = on;
    std::vector<float> out = in;
    for (int pos = 0; pos < n; pos += 100) fx.master_out(m, &out[size_t(2 * pos)], std::min(100, n - pos));
    const int d = twill::Effects::kLimiterDelay;
    float quiet_err = 0, loud_peak = 0;
    for (int i = d; i < n / 2; ++i)
      quiet_err = std::max(quiet_err, std::fabs(out[size_t(2 * i)] - (on ? in[size_t(2 * (i - d))] : std::tanh(in[size_t(2 * (i - d))]))));
    for (int i = n / 2 + d; i < n; ++i) loud_peak = std::max(loud_peak, std::fabs(out[size_t(2 * i)]));
    CHECK_MSG(quiet_err < 1e-6f, "limiter %s: quiet audio changed by %g", on ? "on" : "off", quiet_err);
    if (on) CHECK_MSG(loud_peak <= 0.9661f && loud_peak > 0.9f, "limited peak %.4f", loud_peak);
    else CHECK_MSG(loud_peak > 0.99f, "soft-clipped peak %.4f", loud_peak);
  }
}

void test_golden_render() {
  std::vector<float> out = render(golden_project(), 4.0f);
  std::string path = g_golden_dir + "/m2-three-tracks.wav";
  if (g_update_golden) {
    CHECK(write_wav(path, out));
    std::printf("    wrote %s\n", path.c_str());
    return;
  }
  std::vector<float> ref;
  CHECK_MSG(read_wav(path, ref), "can't read %s (run with --update-golden to make it)", path.c_str());
  if (ref.size() != out.size()) {
    CHECK_MSG(false, "length %zu, golden %zu", out.size(), ref.size());
    return;
  }
  // Compare at the golden file's 16-bit resolution. On the machine that made it the match is
  // exact; elsewhere, within -40 dB leaves room for floating-point differences between compilers
  // and CPUs, while any change in timing, pitch or level fails.
  size_t identical = 0;
  double diff = 0, energy = 0;
  for (size_t i = 0; i < out.size(); ++i) {
    float q = std::lround(std::max(-1.0f, std::min(1.0f, out[i])) * 32767) / 32767.0f;
    identical += q == ref[i];
    double d = q - ref[i];
    diff += d * d;
    energy += double(ref[i]) * ref[i];
  }
  double db = 10 * std::log10((diff + 1e-20) / (energy + 1e-20));
  if (identical == out.size()) std::printf("    identical to golden\n");
  else std::printf("    difference from golden: %.1f dB (%zu of %zu samples differ)\n", db, out.size() - identical, out.size());
  CHECK_MSG(db < -40, "%.1f dB", db);
}

// --- Sampler ----------------------------------------------------------------------------------

// A one-second test sample: a 440 Hz sine on the left, silence on the right, 44.1 kHz mono-or-stereo.
std::vector<uint8_t> sine_wav(int rate, int channels, float seconds = 1.0f) {
  const int frames = int(rate * seconds);
  std::vector<float> st(size_t(frames) * 2);
  for (int i = 0; i < frames; ++i) st[size_t(2 * i)] = st[size_t(2 * i + 1)] = 0.5f * std::sin(6.2831853f * 440 * i / rate);
  std::vector<uint8_t> wav = twill::encode_wav16(st.data(), frames, rate);
  if (channels == 1) {  // rewrite as mono: header fields and every other sample
    std::vector<uint8_t> mono(wav.begin(), wav.begin() + 44);
    auto put16 = [&](size_t at, uint16_t v) { mono[at] = uint8_t(v); mono[at + 1] = uint8_t(v >> 8); };
    auto put32 = [&](size_t at, uint32_t v) { for (int k = 0; k < 4; ++k) mono[at + size_t(k)] = uint8_t(v >> (8 * k)); };
    put16(22, 1);
    put32(28, uint32_t(rate * 2));
    put16(32, 2);
    put32(40, uint32_t(frames * 2));
    put32(4, uint32_t(36 + frames * 2));
    for (int i = 0; i < frames; ++i) mono.insert(mono.end(), wav.begin() + 44 + 4 * i, wav.begin() + 46 + 4 * i);
    return mono;
  }
  return wav;
}

void test_sample_decode_and_resample() {
  std::string err;
  std::vector<uint8_t> w48 = sine_wav(48000, 2), w44 = sine_wav(44100, 1);
  auto a = twill::decode_sample(w48.data(), w48.size(), "a", err);
  auto b = twill::decode_sample(w44.data(), w44.size(), "b", err);
  CHECK(a && a->frames == 48000 && a->source_channels == 2);
  CHECK(b && b->source_rate == 44100 && b->source_channels == 1);
  CHECK_MSG(b && std::abs(b->frames - 48000) <= 1, "resampled to %d frames", b ? b->frames : 0);
  // Resampled audio keeps its pitch: compare a few samples with the ideal 440 Hz sine at 48 kHz.
  double err_sum = 0;
  for (int i = 1000; b && i < 1100; ++i) err_sum += std::fabs(b->left(i) - 0.5 * std::sin(6.2831853 * 440 * i / 48000));
  CHECK_MSG(err_sum / 100 < 0.01, "mean error %.4f", err_sum / 100);
  std::vector<uint8_t> junk(100, 7);
  CHECK(!twill::decode_sample(junk.data(), junk.size(), "junk", err));
}

// A sampler track whose sample is `sample`, and the resolver the snapshot needs.
struct SamplerFixture {
  std::shared_ptr<const twill::SampleData> sample;
  twill::Project p = make_project();
  explicit SamplerFixture(std::shared_ptr<const twill::SampleData> s) : sample(std::move(s)) {
    p.tracks[0].instrument = twill::InstrumentType::Sampler;
    p.tracks[0].sampler.sample = "test.wav";
    p.tracks[0].voices = 4;
  }
  std::vector<float> render(float seconds) {
    twill::Engine e(16);
    e.submit(twill::build_snapshot(p, 16, 1, [&](const std::string&) { return sample.get(); }));
    e.play();
    std::vector<float> out(size_t(seconds * kRate) * 2);
    for (size_t pos = 0; pos < out.size() / 2; pos += 256)
      e.render(&out[pos * 2], int(std::min<size_t>(256, out.size() / 2 - pos)));
    return out;
  }
};

// Zero crossings per second on the left channel: twice the frequency.
double frequency(const std::vector<float>& st, int from, int to) {
  int crossings = 0;
  for (int i = from + 1; i < to; ++i) crossings += (st[size_t(2 * i - 2)] < 0) != (st[size_t(2 * i)] < 0);
  return crossings / 2.0 / (double(to - from) / kRate);
}

void test_sampler_pitch_follows_note() {
  std::string err;
  std::vector<uint8_t> w = sine_wav(48000, 2);
  SamplerFixture f(twill::decode_sample(w.data(), w.size(), "s", err));
  f.p.clips[0].notes = {note_at(0, 60, 2 * Q), note_at(4 * Q, 72, 2 * Q)};  // root, then an octave up
  std::vector<float> out = f.render(3.0f);
  const double f1 = frequency(out, 2000, 20000), f2 = frequency(out, 98000, 116000);
  CHECK_MSG(std::fabs(f1 - 440) < 8, "root note plays at %.1f Hz", f1);
  CHECK_MSG(std::fabs(f2 - 880) < 12, "an octave up plays at %.1f Hz", f2);
}

void test_sampler_slices_and_kit() {
  // A sample of eight equal "slices", each a constant level 1/8, 2/8, ...
  auto s = std::make_shared<twill::SampleData>();
  s->frames = 8000;
  s->data.resize(16000);
  for (int i = 0; i < 8000; ++i) s->data[size_t(2 * i)] = s->data[size_t(2 * i + 1)] = (i / 1000 + 1) / 8.0f;
  SamplerFixture f(s);
  f.p.tracks[0].sampler.mode = twill::SamplerMode::Slice;
  f.p.tracks[0].sampler.slices = 8;
  f.p.tracks[0].sampler.root = 60;
  f.p.tracks[0].level = 1;
  f.p.tracks[0].tone.attack = 0;
  f.p.clips[0].notes = {note_at(0, 63, Q / 4)};  // slice 4: level 4/8
  std::vector<float> out = f.render(0.1f);
  // Mid-slice, past the attack: the envelope is up and the level is slice 4's.
  const float v = out[2 * 600];
  CHECK_MSG(std::fabs(v - std::tanh(0.5f * 100 / 127.0f)) < 0.05f, "slice 4 level %.3f", v);
  f.p.clips[0].notes = {note_at(0, 59, Q / 4)};  // below the root: no slice, silence
  out = f.render(0.1f);
  CHECK(*std::max_element(out.begin(), out.end()) == 0);

  // Kit: note 36 + i plays slot i; an empty slot is silent.
  f.p.tracks[0].sampler.mode = twill::SamplerMode::Kit;
  f.p.tracks[0].sampler.kit[2].sample = "test.wav";
  f.p.clips[0].notes = {note_at(0, twill::kKitFirstNote + 2, Q / 4)};
  out = f.render(0.05f);
  CHECK(*std::max_element(out.begin(), out.end()) > 0.05f);
  f.p.clips[0].notes = {note_at(0, twill::kKitFirstNote + 3, Q / 4)};
  out = f.render(0.05f);
  CHECK(*std::max_element(out.begin(), out.end()) == 0);
}

// In a kit, Voices counts per drum: two drums at once both play even at one voice, while a drum
// hit again still cuts off its own last hit.
// Slicing at transients: a "drum loop" of four decaying noise hits at uneven places and levels.
void test_sampler_transient_slices() {
  auto s = std::make_shared<twill::SampleData>();
  s->frames = 48000;
  s->data.assign(96000, 0.0f);
  const int hits[4] = {0, 9000, 21000, 33500};
  const float levels[4] = {0.9f, 0.6f, 0.15f, 0.8f};
  uint32_t rng = 1;
  for (int h = 0; h < 4; ++h)
    for (int i = 0; i < 6000; ++i) {
      rng = rng * 1664525u + 1013904223u;
      const float noise = (rng >> 8) / float(1 << 24) * 2 - 1;
      const float x = levels[h] * noise * std::exp(-i / 1200.0f);
      s->data[size_t(2 * (hits[h] + i))] = s->data[size_t(2 * (hits[h] + i) + 1)] = x;
    }
  s->onsets = twill::find_onsets(*s);
  // The hit at the very start is the sample's start, not an onset.
  CHECK_MSG(s->onsets.size() == 3, "%zu onsets", s->onsets.size());
  for (size_t i = 0; i < s->onsets.size() && i < 3; ++i)
    CHECK_MSG(s->onsets[i].frame <= hits[i + 1] && s->onsets[i].frame > hits[i + 1] - 100, "onset %zu at %d", i,
              s->onsets[i].frame);

  std::vector<int> all = twill::transient_slices(*s, 0, 1, 16);
  CHECK(all.size() == 5 && all.front() == 0 && all.back() == 48000);
  // Room for three slices: the quietest hit isn't a slice of its own.
  std::vector<int> three = twill::transient_slices(*s, 0, 1, 3);
  CHECK(three.size() == 4);
  if (three.size() == 4) CHECK(std::abs(three[1] - 9000) < 100 && std::abs(three[2] - 33500) < 100);

  // The engine plays slice 2 (root + 2) from the third hit.
  SamplerFixture f(s);
  f.p.tracks[0].sampler.mode = twill::SamplerMode::Slice;
  f.p.tracks[0].sampler.slice_transients = true;
  f.p.tracks[0].sampler.root = 60;
  f.p.tracks[0].tone.attack = 0;
  f.p.clips[0].notes = {note_at(0, 62, Q)};
  std::vector<float> out = f.render(0.2f);
  const int d = twill::Engine::kOutputDelay;
  float first = 0;
  for (int i = d; i < d + 200; ++i) first = std::max(first, std::fabs(out[size_t(2 * i)]));
  CHECK_MSG(first > 0.05f && first < 0.2f, "slice 2 starts at level %.3f", first);  // the 0.15 hit, not the 0.9
  // And slice 3 ends where the sample does: no slice 4.
  f.p.clips[0].notes = {note_at(0, 64, Q)};
  out = f.render(0.1f);
  float any = 0;
  for (float x : out) any = std::max(any, std::fabs(x));
  CHECK(any == 0);
}

void test_kit_voices_per_drum() {
  auto s = std::make_shared<twill::SampleData>();
  s->frames = 48000;  // one second of a constant level, so overlapping hits add up
  s->data.assign(96000, 0.1f);
  SamplerFixture f(s);
  f.p.tracks[0].sampler.mode = twill::SamplerMode::Kit;
  f.p.tracks[0].sampler.kit[0].sample = "test.wav";
  f.p.tracks[0].sampler.kit[2].sample = "test.wav";
  f.p.tracks[0].voices = 1;
  f.p.tracks[0].level = 1;
  f.p.tracks[0].tone.attack = 0;
  f.p.tracks[0].tone.sustain = 1;
  auto level_at = [&](int frame) {
    std::vector<float> out = f.render(0.4f);
    return out[2 * size_t(frame)];
  };
  f.p.clips[0].notes = {note_at(0, twill::kKitFirstNote, Q)};
  const float one = level_at(2400);
  f.p.clips[0].notes = {note_at(0, twill::kKitFirstNote, Q), note_at(0, twill::kKitFirstNote + 2, Q)};
  const float two = level_at(2400);
  CHECK_MSG(two > 1.8f * one, "kick %.3f, kick + snare %.3f", one, two);
  f.p.clips[0].notes = {note_at(0, twill::kKitFirstNote, Q), note_at(Q / 4, twill::kKitFirstNote, Q)};
  const float again = level_at(9600);  // after the second hit: it replaced the first
  CHECK_MSG(again < 1.2f * one, "kick then kick %.3f", again);
}

void test_kit_one_shot() {
  // Kit drums play to the end of the sample: a very short note with the shortest release still
  // sounds after the note has ended. Voices per drum still decide whether hits of the same drum
  // overlap, and Single mode still stops at the note's end.
  auto s = std::make_shared<twill::SampleData>();
  s->frames = 48000;  // one second at a constant level
  s->data.assign(96000, 0.1f);
  SamplerFixture f(s);
  twill::Track& t = f.p.tracks[0];
  t.sampler.mode = twill::SamplerMode::Kit;
  t.sampler.kit[0].sample = "test.wav";
  t.voices = 4;
  t.level = 1;
  t.tone.attack = 0;
  t.tone.sustain = 1;
  t.tone.release = 0;
  auto at = [](const std::vector<float>& o, double seconds) { return std::fabs(o[2 * size_t(seconds * 48000)]); };
  f.p.clips[0].notes = {note_at(0, twill::kKitFirstNote, Q / 16)};
  std::vector<float> out = f.render(1.2f);
  const float early = at(out, 0.02), late = at(out, 0.8), after = at(out, 1.1);
  CHECK_MSG(late > 0.9f * early, "kit drum at 0.02 s %.3f, at 0.8 s %.3f", early, late);
  CHECK_MSG(after == 0, "after the sample ends %.3f", after);
  // At one voice per drum, a second hit still replaces the first.
  t.voices = 1;
  f.p.clips[0].notes = {note_at(0, twill::kKitFirstNote, Q / 16), note_at(Q / 2, twill::kKitFirstNote, Q / 16)};
  out = f.render(1.2f);
  const float again = at(out, 0.5);  // well after the second hit (0.25 s at the fixture's tempo)
  CHECK_MSG(again < 1.2f * early, "hit twice %.3f, once %.3f", again, early);
  // At more, they overlap.
  t.voices = 4;
  out = f.render(1.2f);
  CHECK_MSG(at(out, 0.5) > 1.8f * early, "hit twice at 4 voices %.3f, once %.3f", at(out, 0.5), early);
  // A slot's decay fades the hit out: at 0.5 (about 300 ms to -60 dB) it's quieter at 0.1 s, gone by
  // 0.5 s, and the full sample still plays when it's back at 1.
  t.sampler.kit[0].decay = 0.5f;
  CHECK(twill::kit_decay_seconds(0.5f) > 0.25f && twill::kit_decay_seconds(0.5f) < 0.4f);
  CHECK(twill::kit_decay_seconds(1) == 0);
  f.p.clips[0].notes = {note_at(0, twill::kKitFirstNote, Q / 16)};
  out = f.render(1.2f);
  CHECK_MSG(at(out, 0.1) < 0.5f * early && at(out, 0.1) > 0.05f * early, "decay 0.5 at 0.1 s %.4f", at(out, 0.1));
  CHECK_MSG(at(out, 0.5) == 0, "decay 0.5 at 0.5 s %.4f", at(out, 0.5));
  t.sampler.kit[0].decay = 1;
  t.sampler.mode = twill::SamplerMode::Single;
  t.sampler.sample = "test.wav";
  f.p.clips[0].notes = {note_at(0, 60, Q / 16)};
  out = f.render(1.2f);
  CHECK_MSG(at(out, 0.8) < 0.01f * early, "single mode after the note %.3f", at(out, 0.8));
}

void test_sampler_loop_sustains() {
  // A 0.1 s sample, looped, under a 1 s note keeps sounding; unlooped it stops.
  std::string err;
  std::vector<uint8_t> w = sine_wav(48000, 2, 0.1f);
  SamplerFixture f(twill::decode_sample(w.data(), w.size(), "s", err));
  f.p.clips[0].notes = {note_at(0, 60, 2 * Q)};
  auto tail_peak = [](const std::vector<float>& o) {
    float pk = 0;
    for (size_t i = 2 * 24000; i < 2 * 30000; ++i) pk = std::max(pk, std::fabs(o[i]));
    return pk;
  };
  CHECK(tail_peak(f.render(1.0f)) == 0);
  f.p.tracks[0].sampler.loop = twill::LoopMode::Forward;
  CHECK(tail_peak(f.render(1.0f)) > 0.1f);
  f.p.tracks[0].sampler.loop = twill::LoopMode::PingPong;
  CHECK(tail_peak(f.render(1.0f)) > 0.1f);
}

void test_filter_darkens() {
  // White-ish noise through the low-pass at a low cutoff loses most of its high frequencies.
  auto s = std::make_shared<twill::SampleData>();
  s->frames = 48000;
  s->data.resize(96000);
  uint32_t x = 1;
  for (float& v : s->data) {
    x = x * 1664525u + 1013904223u;
    v = (int32_t(x) >> 8) / float(1 << 23) * 0.5f;
  }
  SamplerFixture f(s);
  f.p.clips[0].notes = {note_at(0, 60, 2 * Q)};
  auto hf = [](const std::vector<float>& o) {  // energy of the first difference: high frequencies
    double e = 0;
    for (size_t i = 2 * 2000 + 2; i < 2 * 20000; i += 2) e += double(o[i] - o[i - 2]) * (o[i] - o[i - 2]);
    return e;
  };
  const double open = hf(f.render(0.5f));
  f.p.tracks[0].tone.filter = twill::FilterType::LowPass;
  f.p.tracks[0].tone.cutoff = 0.3f;  // about 160 Hz
  const double closed = hf(f.render(0.5f));
  CHECK_MSG(closed < open * 0.05, "high-frequency energy %.3g open, %.3g filtered", open, closed);
}

void test_modulators() {
  // An LFO on pitch makes a steady sine waver; an envelope on cutoff makes noise bright, then dark.
  std::string err;
  std::vector<uint8_t> w = sine_wav(48000, 2, 2.0f);
  SamplerFixture f(twill::decode_sample(w.data(), w.size(), "s", err));
  f.p.clips[0].notes = {note_at(0, 60, 4 * Q)};
  const std::vector<float> plain = f.render(1.5f);
  twill::Modulator& lfo = f.p.tracks[0].mods[0];
  lfo.type = twill::ModType::Lfo;
  lfo.dest = twill::kParamPitch;
  lfo.depth = 0.5f;  // +-12 semitones
  lfo.rate = 0.25f;  // about 0.22 Hz: one slow sweep across the test
  const std::vector<float> wobbly = f.render(1.5f);
  const double a = frequency(wobbly, 2000, 14000), b = frequency(wobbly, 48000, 60000);
  CHECK_MSG(std::fabs(a - b) > 40, "pitch LFO: %.0f Hz then %.0f Hz", a, b);
  CHECK(std::fabs(frequency(plain, 2000, 14000) - frequency(plain, 48000, 60000)) < 8);

  auto noise = std::make_shared<twill::SampleData>();
  noise->frames = 96000;
  noise->data.resize(192000);
  uint32_t x = 7;
  for (float& v : noise->data) {
    x = x * 1664525u + 1013904223u;
    v = (int32_t(x) >> 8) / float(1 << 23) * 0.5f;
  }
  SamplerFixture g(noise);
  g.p.clips[0].notes = {note_at(0, 60, 4 * Q)};
  g.p.tracks[0].tone.filter = twill::FilterType::LowPass;
  g.p.tracks[0].tone.cutoff = 0.2f;
  twill::Modulator& env = g.p.tracks[0].mods[2];
  env.type = twill::ModType::Envelope;
  env.dest = twill::kParamCutoff;
  env.depth = 0.8f;
  env.decay = 0.45f;  // about 60 ms
  std::vector<float> out = g.render(1.0f);
  auto hf = [&](int from, int to) {
    double e = 0;
    for (int i = from + 1; i < to; ++i) e += std::pow(double(out[size_t(2 * i)] - out[size_t(2 * i - 2)]), 2);
    return e;
  };
  CHECK_MSG(hf(500, 2500) > 20 * hf(30000, 32000), "bright %.3g, later %.3g", hf(500, 2500), hf(30000, 32000));
}

void test_lfo_phase_modes() {
  // A square LFO on pitch (+-12 semitones): high for the first half of each cycle, low after.
  // Returns the pitch near the start of a note at `at`.
  auto pitch_at = [](Tick at, bool sync, bool retrigger, float rate) {
    std::string err;
    std::vector<uint8_t> w = sine_wav(48000, 2, 2.0f);
    SamplerFixture f(twill::decode_sample(w.data(), w.size(), "s", err));
    f.p.clips[0].notes = {note_at(0, 60, Q), note_at(at, 60, Q)};
    twill::Modulator& lfo = f.p.tracks[0].mods[0];
    lfo.type = twill::ModType::Lfo;
    lfo.dest = twill::kParamPitch;
    lfo.depth = 0.5f;
    lfo.shape = twill::LfoShape::Square;
    lfo.sync = sync;
    lfo.rate = rate;
    lfo.retrigger = retrigger;
    const std::vector<float> out = f.render(2.5f);
    const int s = int(int64_t(at) * kRate * 60 / (120 * Q));  // 120 BPM
    return frequency(out, s + 1000, s + 6000);
  };
  // Free, about 0.67 Hz (1.5 s a cycle): a note 1 s in starts high when it restarts the LFO, low
  // when the LFO runs on.
  const float free_rate = std::log(0.667f / 0.05f) / std::log(400.0f);
  const double each = pitch_at(2 * Q, false, true, free_rate), run = pitch_at(2 * Q, false, false, free_rate);
  CHECK_MSG(each > 1.5 * run, "free LFO: each note %.0f Hz, running %.0f Hz", each, run);
  // Synced to 1 bar: a note on beat 3 is in the low half when following the song.
  const float bar = 2 / 6.0f;
  const double each_s = pitch_at(2 * Q, true, true, bar), song = pitch_at(2 * Q, true, false, bar);
  CHECK_MSG(each_s > 1.5 * song, "synced LFO: each note %.0f Hz, following the song %.0f Hz", each_s, song);
  // ... and on the next downbeat it is high again, whatever came before.
  const double downbeat = pitch_at(4 * Q, true, false, bar);
  CHECK_MSG(downbeat > 1.5 * song, "synced LFO on the downbeat: %.0f Hz", downbeat);
}

void test_automation_lane() {
  // A level lane from 0 to 1 across the clip: quiet at first, loud at the end.
  std::string err;
  std::vector<uint8_t> w = sine_wav(48000, 2, 2.0f);
  SamplerFixture f(twill::decode_sample(w.data(), w.size(), "s", err));
  f.p.clips[0].notes = {note_at(0, 60, 4 * Q)};
  twill::Lane lane;
  lane.param = twill::kParamLevel;
  lane.set_point(0, 0);
  lane.set_point(4 * Q, 1);
  f.p.clips[0].lanes = {lane};
  std::vector<float> out = f.render(1.9f);
  auto peak = [&](int from, int to) {
    float pk = 0;
    for (int i = from; i < to; ++i) pk = std::max(pk, std::fabs(out[size_t(2 * i)]));
    return pk;
  };
  const float early = peak(1000, 5000), late = peak(80000, 88000);
  CHECK_MSG(early < 0.1f && late > 0.25f, "level lane: %.3f early, %.3f late", early, late);
  CHECK(lane.value_at(2 * Q) == 0.5f);
  CHECK(lane.value_at(-10) == 0 && lane.value_at(99 * Q) == 1);
}

void test_send_effects() {
  // A 10 ms burst. Sent to the delay at 1/4 (120 BPM: 24000 samples), it echoes there.
  auto click = std::make_shared<twill::SampleData>();
  click->frames = 480;
  click->data.assign(960, 0.5f);
  SamplerFixture f(click);
  f.p.tracks[0].tone.attack = 0;
  f.p.clips[0].notes = {note_at(0, 60, Q / 8)};
  auto energy = [](const std::vector<float>& o, int from, int to) {
    double e = 0;
    for (int i = from; i < to; ++i) e += double(o[size_t(2 * i)]) * o[size_t(2 * i)] + double(o[size_t(2 * i + 1)]) * o[size_t(2 * i + 1)];
    return e;
  };
  const std::vector<float> dry = f.render(1.0f);
  CHECK(energy(dry, 1000, 48000) == 0);  // nothing after the click without sends

  f.p.tracks[0].send_delay = 1;
  f.p.mix.delay_time = 4 / 6.0f;  // 1/4
  f.p.mix.delay_feedback = 0;
  const std::vector<float> echo = f.render(1.0f);
  CHECK_MSG(energy(echo, 24000, 24500) > 1, "echo energy %.4f", energy(echo, 24000, 24500));
  CHECK(energy(echo, 1000, 23800) < 1e-6);

  f.p.tracks[0].send_delay = 0;
  f.p.tracks[0].send_reverb = 1;
  const std::vector<float> wet = f.render(1.0f);
  CHECK_MSG(energy(wet, 4800, 24000) > 1e-4, "reverb tail energy %.6f", energy(wet, 4800, 24000));
}

// Lanes for the effects and the master, on any track: here a second track's clip (with no notes)
// moves the delay from 1/4 to 1/2, and the master level fades out.
void test_effect_automation() {
  auto click = std::make_shared<twill::SampleData>();
  click->frames = 480;
  click->data.assign(960, 0.5f);
  SamplerFixture f(click);
  f.p.tracks[0].send_delay = 1;
  f.p.mix.delay_time = 4 / 6.0f;  // 1/4: 24000 samples at 120 BPM
  f.p.mix.delay_feedback = 0;
  f.p.clips[0].notes = {note_at(0, 60, Q / 8), note_at(4 * Q, 60, Q / 8)};
  twill::Track other;
  f.p.tracks.push_back(other);
  twill::Clip& c = f.p.add_clip(1, 16 * Q);
  f.p.tracks[1].arrangement.push_back(twill::Placement{0, c.id, 0, 16 * Q});
  twill::Lane delay;
  delay.param = twill::kParamDelayTime;
  delay.set_point(0, 1);  // 1/2
  twill::Lane fade;
  fade.param = twill::kParamMasterLevel;
  fade.set_point(0, 1);
  fade.set_point(2 * Q, 1);
  fade.set_point(3 * Q, 0);
  c.lanes = {delay, fade};
  CHECK(twill::automatable_params(twill::InstrumentType::Macro).back() == twill::kParamMasterLevel);
  auto energy = [](const std::vector<float>& o, int from, int to) {
    double e = 0;
    for (int i = from; i < to; ++i) e += double(o[size_t(2 * i)]) * o[size_t(2 * i)];
    return e;
  };
  const std::vector<float> out = f.render(2.5f);
  CHECK_MSG(energy(out, 23800, 24600) < 1e-6, "echo at 1/4: %.4f", energy(out, 23800, 24600));
  CHECK_MSG(energy(out, 48000, 48500) > 0.1, "echo at 1/2: %.4f", energy(out, 48000, 48500));
  // Faded out by beat 3: the second click, on beat 4, is silent.
  CHECK_MSG(energy(out, 96000, 97000) < 1e-9, "after the fade: %.6f", energy(out, 96000, 97000));
}

// --- MIDI -----------------------------------------------------------------------------------

void test_midi_parser() {
  twill::MidiParser p;
  std::vector<twill::MidiMessage> out;
  // Note on, then two more under running status, with a clock byte in the middle of one.
  const uint8_t bytes[] = {0x91, 60, 100, 62, 0xF8, 90, 64, 0, 0xF0, 1, 2, 3, 0xF7, 0xC2, 5, 0xB0, 7, 99};
  p.feed(bytes, sizeof bytes, out);
  CHECK(out.size() == 6);
  if (out.size() == 6) {
    CHECK(out[0].is_note_on() && out[0].channel() == 1 && out[0].data1 == 60 && out[0].data2 == 100);
    CHECK(out[1].status == 0xF8);
    CHECK(out[2].is_note_on() && out[2].data1 == 62 && out[2].data2 == 90);
    CHECK(out[3].is_note_off() && out[3].data1 == 64);  // velocity 0
    CHECK(out[4].type() == 0xC0 && out[4].data1 == 5 && out[4].size() == 2);
    CHECK(out[5].type() == 0xB0 && out[5].data1 == 7 && out[5].data2 == 99);
  }
  // A message split across two feeds.
  out.clear();
  const uint8_t a[] = {0x80, 60}, b[] = {0};
  p.feed(a, 2, out);
  CHECK(out.empty());
  p.feed(b, 1, out);
  CHECK(out.size() == 1 && out[0].is_note_off());
}

// MIDI out events, without the anchors, as (sample, message).
std::vector<std::pair<uint64_t, twill::MidiMessage>> drain_midi(twill::Engine& e) {
  std::vector<std::pair<uint64_t, twill::MidiMessage>> got;
  twill::Engine::MidiEvent ev;
  while (e.pop_midi_out(ev))
    if (ev.kind != twill::Engine::MidiEvent::Anchor) got.push_back({ev.sample, ev.m});
  return got;
}

void test_midi_out_track() {
  twill::Project p = make_project();  // 120 BPM: a beat is 24000 samples
  p.tracks[0].instrument = twill::InstrumentType::MidiOut;
  p.tracks[0].midi_channel = 4;
  p.clips[0].notes = {note_at(0, 60, Q / 2), note_at(Q, 64, Q / 2)};
  twill::Engine e(16);
  e.submit(twill::build_snapshot(p, 16, 1));
  e.play();
  std::vector<float> buf(512);
  std::vector<std::pair<uint64_t, twill::MidiMessage>> got;
  for (int b = 0; b < 200; ++b) {  // a little over a second
    e.render(buf.data(), 256);
    for (auto& x : drain_midi(e)) got.push_back(x);
  }
  CHECK(got.size() == 4);
  if (got.size() == 4) {
    // Stamped to the sample: on at 0, off half a beat later, the next on a beat in.
    CHECK(got[0].second.status == 0x94 && got[0].second.data1 == 60 && got[0].first == 0);
    CHECK_MSG(got[1].second.status == 0x84 && got[1].first == 12000, "note off at %llu", (unsigned long long)got[1].first);
    CHECK_MSG(got[2].second.status == 0x94 && got[2].second.data1 == 64 && got[2].first == 24000,
              "second note at %llu", (unsigned long long)got[2].first);
  }
  // A note still sounding when the transport stops gets its note off at once.
  e.locate(0);
  e.play();
  e.render(buf.data(), 256);
  e.stop();
  e.render(buf.data(), 256);
  int ons = 0, offs = 0;
  for (auto& x : drain_midi(e)) (x.second.is_note_on() ? ons : offs) += 1;
  CHECK(ons == 1 && offs == 1);
}

// Controller lanes on a MIDI Out track send a CC when the lane's value changes; a note's lock sends
// its value just before the note and holds it until the note ends.
void test_midi_out_cc() {
  twill::Project p = make_project();  // 120 BPM: a beat is 24000 samples
  twill::Track& t = p.tracks[0];
  t.instrument = twill::InstrumentType::MidiOut;
  t.midi_channel = 2;
  t.midi_cc[0] = 74;
  t.midi_cc[1] = 1;
  twill::Lane lane;
  lane.param = twill::kParamMidiCcA;
  lane.set_point(0, 0);
  lane.set_point(4 * Q, 127);  // a ramp over two seconds
  p.clips[0].lanes.push_back(lane);
  twill::Note n = note_at(2 * Q, 60, Q);
  n.set_lock(twill::kParamMidiCcA, 10);
  n.set_lock(twill::kParamMidiCcB, 99);
  p.clips[0].notes = {n};
  twill::Engine e(16);
  e.submit(twill::build_snapshot(p, 16, 1));
  e.play();
  std::vector<float> buf(512);
  std::vector<std::pair<uint64_t, twill::MidiMessage>> got;
  for (int b = 0; b < 400; ++b) {  // about two seconds
    e.render(buf.data(), 256);
    for (auto& x : drain_midi(e)) got.push_back(x);
  }
  std::vector<std::pair<uint64_t, int>> a;  // CC 74
  int b_count = 0;
  bool b_before_note = false;
  for (size_t i = 0; i < got.size(); ++i) {
    const twill::MidiMessage& m = got[i].second;
    if (m.type() == 0xB0) {
      CHECK(m.channel() == 2);
      if (m.data1 == 74) a.push_back({got[i].first, m.data2});
      if (m.data1 == 1) {
        ++b_count;
        CHECK(m.data2 == 99);
        b_before_note = i + 1 < got.size() && got[i + 1].second.is_note_on();
      }
    }
  }
  CHECK(b_count == 1);  // no lane on CC B: only the lock's value
  CHECK(b_before_note);
  CHECK(!a.empty() && a.front().first == 0 && a.front().second == 0);
  // Rising, one message per new value, except under the note (beat 2 to 3): the lock's 10 holds.
  bool rising_outside = true, lock_held = true;
  for (size_t i = 1; i < a.size(); ++i) {
    const bool under_note = a[i].first >= 48000 && a[i].first < 72000;
    if (under_note) lock_held = lock_held && a[i].second == 10;
    else if (a[i - 1].first < 48000 || a[i - 1].first >= 72000) rising_outside = rising_outside && a[i].second > a[i - 1].second;
  }
  CHECK(rising_outside);
  CHECK(lock_held);
  // One message per new value (the ramp less the quarter the note covers), not one per block.
  CHECK_MSG(a.size() > 90 && a.size() < 110, "%zu CC messages", a.size());
}

// The clock follower on its own: the tempo from jittery pulses, a tempo change, song position.
void test_clock_follower() {
  twill::ClockFollower f;
  int64_t t = 1000000;
  uint32_t rng = 7;
  auto pulses = [&](int n, double bpm) {
    const double every = 60e6 / (bpm * 24);
    for (int i = 0; i < n; ++i) {
      rng = rng * 1664525u + 1013904223u;
      twill::MidiMessage m{0xF8, 0, 0};
      m.time_us = t + int64_t((rng >> 8) % 2000) - 1000;  // +-1 ms of jitter
      f.feed(m);
      t += int64_t(every);
    }
  };
  twill::MidiMessage spp{0xF2, 8, 1};  // sixteenth 136: bar 9 in 4/4
  twill::ClockFollower::Action a = f.feed(spp);
  CHECK(a.kind == twill::ClockFollower::Action::Locate && a.position == 136 * Q / 4);
  pulses(96, 128);
  CHECK_MSG(f.has_tempo() && std::fabs(f.bpm() - 128) < 0.3, "%.2f BPM", f.bpm());
  pulses(48, 90);  // two beats after a change
  CHECK_MSG(std::fabs(f.bpm() - 90) < 0.5, "after the change: %.2f BPM", f.bpm());
  twill::MidiMessage cont{0xFB, 0, 0};
  CHECK(f.feed(cont).kind == twill::ClockFollower::Action::None);  // waits for the next pulse
  twill::MidiMessage clock{0xF8, 0, 0};
  clock.time_us = t;
  a = f.feed(clock);
  CHECK(a.kind == twill::ClockFollower::Action::Play && !a.from_start && f.running());
  CHECK(f.feed(twill::MidiMessage{0xFC, 0, 0}).kind == twill::ClockFollower::Action::Stop);
}

// The sender holds each event until its audio reaches the speaker: the callback's time, plus the
// event's place in it, plus the latency.
void test_midi_sender_timing() {
  twill::Project p = make_project();
  p.tracks[0].instrument = twill::InstrumentType::MidiOut;
  p.clips[0].notes = {note_at(0, 60, Q / 2)};  // on at sample 0
  twill::Engine e(16);
  e.submit(twill::build_snapshot(p, 16, 1));
  e.play();
  twill::NullPlatform::FakeMidi port;
  twill::MidiSender sender(e, &port);
  sender.set_latency_us(10000);
  std::vector<float> buf(512);
  const int64_t before = twill::MidiSender::now_us();
  e.render(buf.data(), 256);
  const int64_t after = twill::MidiSender::now_us();
  sender.pump(before + 9000);  // before the note's audio reaches the speaker
  CHECK(port.sent.empty());
  sender.pump(after + 10001);
  CHECK(port.sent.size() == 1 && port.sent[0].is_note_on());
  // (Later events follow the same mapping; the engine tests check their sample stamps. Rendering
  // more here would run faster than real time, which the sender rightly treats as a stall.)
}

// The sender's own thread, with audio rendered at real-time pace on another: every note goes out.
void test_midi_sender_thread() {
#ifdef __EMSCRIPTEN__
  return;  // no threads in the web build (and no MIDI yet)
#endif
  twill::Project p = make_project();
  p.tracks[0].instrument = twill::InstrumentType::MidiOut;
  p.clips[0].notes = {note_at(0, 60, Q / 8), note_at(Q / 4, 62, Q / 8)};  // 0 and 125 ms
  twill::Engine e(16);
  e.submit(twill::build_snapshot(p, 16, 1));
  e.play();
  twill::NullPlatform::FakeMidi port;
  {
    twill::MidiSender sender(e, &port);
    sender.set_latency_us(10000);
    sender.start_thread();
    std::vector<float> buf(512);
    const auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < 60; ++b) {  // 320 ms of audio, one block every 5.33 ms
      e.render(buf.data(), 256);
      std::this_thread::sleep_until(t0 + std::chrono::microseconds((b + 1) * 5333));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }  // the sender stops here
  int ons = 0, offs = 0;
  for (const twill::MidiMessage& m : port.sent) (m.is_note_on() ? ons : offs) += 1;
  CHECK_MSG(ons == 2 && offs == 2, "%d note ons, %d note offs", ons, offs);
}

void test_midi_clock_out() {
  twill::Project p = make_project();  // 120 BPM: a clock every 1000 samples
  p.midi_clock = twill::MidiClock::Send;
  twill::Engine e(16);
  e.submit(twill::build_snapshot(p, 16, 1));
  std::vector<float> buf(2 * 300);
  e.render(buf.data(), 100);  // stopped: nothing
  CHECK(drain_midi(e).empty());
  e.play();
  for (int b = 0; b < 40; ++b) e.render(buf.data(), 300);  // odd-sized blocks, 0.25 s
  e.stop();
  e.render(buf.data(), 300);
  auto got = drain_midi(e);
  CHECK(!got.empty() && got.front().second.status == 0xFA && got.front().first == 100);
  CHECK(!got.empty() && got.back().second.status == 0xFC);
  std::vector<uint64_t> clocks;
  for (auto& x : got)
    if (x.second.status == 0xF8) clocks.push_back(x.first);
  CHECK_MSG(clocks.size() == 12, "%zu clocks", clocks.size());
  for (size_t i = 0; i < clocks.size(); ++i)
    CHECK_MSG(clocks[i] == 100 + 1000 * i, "clock %zu at %llu", i, (unsigned long long)clocks[i]);
}

void test_live_notes() {
  twill::Project p = make_project();
  p.tracks[0].tone.amp_mode = 1;  // the ADSR, so the note holds while the key is down
  p.tracks[0].tone.release = 0.2f;
  twill::Engine e(16);
  e.submit(twill::build_snapshot(p, 16, 1));
  std::vector<float> buf(512);
  auto peak = [&](int blocks) {
    float pk = 0;
    for (int b = 0; b < blocks; ++b) {
      e.render(buf.data(), 256);
      for (float v : buf) pk = std::max(pk, std::fabs(v));
    }
    return pk;
  };
  CHECK(peak(4) == 0);
  e.live_note(0, 60, 110);
  CHECK(peak(20) > 0.05f);
  CHECK(peak(200) > 0.05f);  // still held, a second later
  e.live_note(0, 60, 0);
  peak(100);
  CHECK(peak(20) < 1e-3f);  // released
}

// --- UI: scripted button presses on the null platform (design §10) ------------------------

// The song the UI tests drive: the demo song as it was until 2026-11-15 (Kick, Snare and Hats
// tracks, then Bass, Chords, Lead, Pluck and Pad), kept fixed so the tests don't depend on the demo.
namespace ui_test_song {
using namespace twill;
namespace {

constexpr Tick Q = kTicksPerQuarter, S = Q / 4, BAR = 4 * Q;

struct Builder {
  Project& p;

  int track(const char* name, int engine, int voices, float level, float pan = 0) {
    Track t;
    t.name = name;
    t.macro.engine = engine;
    t.voices = voices;
    t.level = level;
    t.pan = pan;
    t.colour = uint32_t(p.tracks.size() % 8);
    p.tracks.push_back(t);
    return int(p.tracks.size()) - 1;
  }

  // A clip on `track`, placed at `at` for `placed` ticks (looping the clip if longer).
  Clip& clip(int track, Tick length, Tick at, Tick placed) {
    Clip& c = p.add_clip(track, length);
    p.tracks[size_t(track)].arrangement.push_back(Placement{at, c.id, 0, placed});
    return c;
  }

  static void note(Clip& c, Tick start, int pitch, Tick length, int velocity = 100, bool glide = false) {
    Note n;
    n.id = c.next_note_id++;
    n.start = start;
    n.pitch = uint8_t(pitch);
    n.length = length;
    n.velocity = uint8_t(velocity);
    n.glide = glide;
    c.notes.push_back(n);
  }
};

}  // namespace

Project make() {
  Project p;
  p.name = "Demo";  // the tests rename it to "Eemo"
  p.tempo = 112;
  p.key = 0;
  p.scale = ScaleType::Minor;
  p.loop_start = 0;
  p.loop_end = 8 * BAR;
  Builder b{p};

  int kick = b.track("Kick", 21, 1, 0.9f);
  int snare = b.track("Snare", 22, 1, 0.6f, 0.1f);
  int hat = b.track("Hats", 23, 1, 0.35f, 0.25f);
  int bass = b.track("Bass", 8, 1, 0.7f);
  int chords = b.track("Chords", 13, 3, 0.35f, -0.2f);
  int lead = b.track("Lead", 11, 1, 0.45f, 0.15f);
  int pluck = b.track("Pluck", 19, 3, 0.35f, -0.35f);
  int pad = b.track("Pad", 6, 3, 0.25f, 0.3f);
  p.tracks[size_t(bass)].macro.decay = 0.35f;
  p.tracks[size_t(pad)].macro.timbre = 0.3f;

  Clip& k = b.clip(kick, BAR, 0, 8 * BAR);
  for (Tick t : {Tick(0), Q, 2 * Q, 3 * Q, 3 * Q + 2 * S}) Builder::note(k, t, 36, S, t % Q ? 80 : 110);

  Clip& sn = b.clip(snare, BAR, 2 * BAR, 6 * BAR);
  Builder::note(sn, Q, 55, S);
  Builder::note(sn, 3 * Q, 55, S);

  Clip& h = b.clip(hat, BAR, 0, 8 * BAR);
  for (int i = 0; i < 8; ++i) Builder::note(h, i * 2 * S, 72, S, i % 2 ? 70 : 105);

  // Two bars of bass in C minor, with a slide into the last note.
  Clip& bs = b.clip(bass, 2 * BAR, 0, 8 * BAR);
  const int line[][3] = {{0, 36, 3}, {3, 36, 1}, {6, 48, 2}, {10, 46, 2}, {12, 43, 4},
                         {16, 32, 3}, {19, 32, 1}, {22, 44, 2}, {26, 46, 2}, {28, 43, 2}, {30, 46, 2}};
  for (const auto& n : line) Builder::note(bs, n[0] * S, n[1], n[2] * S, 100, n[0] == 30);

  // Cm7, Abmaj7, Ebmaj7, Bb: one chord per bar.
  Clip& ch = b.clip(chords, 4 * BAR, 0, 8 * BAR);
  const int chord_notes[4][3] = {{60, 63, 67}, {60, 63, 68}, {62, 67, 70}, {62, 65, 70}};
  for (int bar = 0; bar < 4; ++bar)
    for (int pitch : chord_notes[bar]) {
      Builder::note(ch, bar * BAR, pitch, 2 * Q + S, 90);
      Builder::note(ch, bar * BAR + 3 * Q, pitch, Q - S, 70);
    }

  Clip& ld = b.clip(lead, 4 * BAR, 4 * BAR, 4 * BAR);
  const int melody[][3] = {{0, 72, 6}, {6, 75, 2}, {8, 79, 8}, {16, 77, 4}, {20, 75, 4},
                           {24, 74, 8}, {32, 72, 6}, {38, 70, 2}, {40, 67, 8}, {48, 70, 8},
                           {56, 72, 8}};
  for (const auto& n : melody) Builder::note(ld, n[0] * S, n[1], n[2] * S, 100, n[0] == 6 || n[0] == 38);

  Clip& pl = b.clip(pluck, BAR, 2 * BAR, 6 * BAR);
  const int arp[] = {72, 79, 75, 79, 84, 79, 75, 79};
  for (int i = 0; i < 8; ++i) Builder::note(pl, i * 2 * S, arp[i], S, i % 4 ? 75 : 100);

  Clip& pd = b.clip(pad, 4 * BAR, 0, 8 * BAR);
  for (int bar = 0; bar < 4; ++bar)
    for (int pitch : chord_notes[bar]) Builder::note(pd, bar * BAR, pitch - 12, BAR - S, 80);

  for (Clip& c : p.clips) c.sort();
  return p;
}

}  // namespace ui_test_song

struct UiDriver {
  twill::NullPlatform platform;
  twill::App app{platform};
  twill::Canvas canvas;
  UiDriver() {
    canvas.resize(1024, 768);
    // Start on the fixed test song, as if it had been autosaved.
    platform.write_file(twill::App::kAutosaveName, twill::save_project(ui_test_song::make()));
    app.start();
    step(0);
  }
  ~UiDriver() { app.stop(); }
  bool audio = false;  // run the engine as an audio device would, a frame's worth per step
  // One frame with `held` buttons down.
  void step(uint32_t held) {
    if (audio) {
      std::vector<float> buf(2 * 256);
      for (int i = 0; i < 3; ++i) app.engine().render(buf.data(), 256);  // 16 ms
    }
    platform.set_held(held);
    platform.advance_ms(16);
    twill::InputState in;
    platform.poll_input(in);
    app.frame(in, canvas);
    canvas.take_dirty();
  }
  // Press and release `buttons` (as a chord with `hold` held throughout).
  void tap(uint32_t buttons, uint32_t hold = 0) {
    step(hold);
    step(hold | buttons);
    step(hold);
  }
  const twill::Clip& clip() { return *app.open_clip(); }
  int notes_at(Tick start) {
    int n = 0;
    for (const twill::Note& note : clip().notes) n += note.start == start;
    return n;
  }
};

void test_ui_note_panel_start_length_snap() {
  UiDriver ui;
  ui.tap(twill::kRight, twill::kSelect);  // to the piano roll
  ui.tap(twill::kRight);               // an empty 1/16 cell
  ui.tap(twill::kA);                   // a 1/16 note at Q/4
  const Tick fine = Q / 4 / 8;
  auto note = [&]() -> const twill::Note* {
    for (const twill::Note& x : ui.clip().notes)
      if (x.start >= Q / 4 && x.start < Q / 2 && x.pitch == ui.clip().notes.back().pitch) return &x;
    return nullptr;
  };
  const uint8_t pitch = note() ? note()->pitch : 0;
  ui.tap(twill::kX);  // the note's settings: Velocity, Probability, Glide, Start, Length, Snap to grid
  CHECK(ui.app.inspector_open());
  for (int i = 0; i < 3; ++i) ui.tap(twill::kDown);
  ui.tap(twill::kRight);  // Start: an eighth of a step later
  ui.tap(twill::kDown);
  ui.tap(twill::kLeft);   // Length: an eighth of a step shorter
  const twill::Note* n = nullptr;
  for (const twill::Note& x : ui.clip().notes)
    if (x.pitch == pitch && x.start == Q / 4 + fine) n = &x;
  CHECK(n && n->length == Q / 4 - fine);
  ui.tap(twill::kDown);
  ui.tap(twill::kA);      // Snap to grid
  n = nullptr;
  for (const twill::Note& x : ui.clip().notes)
    if (x.pitch == pitch && x.start == Q / 4) n = &x;
  CHECK(n && n->length == Q / 4);
}

void test_ui_copy_paste() {
  UiDriver ui;
  // Roll: select the kicks on beats 1 and 2 (an area from the first cell to the fifth), copy,
  // and paste them a sixteenth later.
  ui.tap(twill::kRight, twill::kSelect);  // the Kick clip, cursor on its first note
  ui.step(twill::kY);
  for (int i = 0; i < 4; ++i) {
    ui.step(twill::kY | twill::kRight);
    ui.step(twill::kY);
  }
  ui.step(0);
  for (int i = 0; i < 4; ++i) ui.tap(twill::kLeft);
  ui.tap(twill::kY, twill::kSelect);  // copy
  CHECK(ui.app.note_clipboard().size() == 2);
  ui.tap(twill::kRight);
  const size_t before = ui.clip().notes.size();
  ui.tap(twill::kR, twill::kSelect);  // paste at the second sixteenth
  CHECK(ui.clip().notes.size() == before + 2);
  CHECK(ui.notes_at(Q / 4) == 1 && ui.notes_at(Q + Q / 4) == 1);
  ui.tap(twill::kB, twill::kSelect);  // one undo takes the whole paste back
  CHECK(ui.clip().notes.size() == before);

  // Arrangement: copy the Kick clip, paste it on the Snare track's empty first bar.
  ui.tap(twill::kLeft, twill::kSelect);  // back to the arrangement
  ui.tap(twill::kY, twill::kSelect);
  ui.tap(twill::kDown);
  ui.tap(twill::kR, twill::kSelect);
  const twill::Project& p = ui.app.project();
  CHECK(!p.tracks[1].arrangement.empty() && p.tracks[1].arrangement.front().start == 0);
  if (!p.tracks[1].arrangement.empty()) {
    const twill::Clip* pasted = p.find_clip(p.tracks[1].arrangement.front().clip_id);
    const twill::Clip* kick = p.find_clip(p.tracks[0].arrangement.front().clip_id);
    CHECK(pasted && kick && pasted != kick && pasted->notes.size() == kick->notes.size());
    CHECK(pasted && pasted->track == 1);
  }
}

void test_ui_roll_fine_nudge() {
  UiDriver ui;
  ui.tap(twill::kRight, twill::kSelect);  // to the piano roll
  ui.tap(twill::kRight);               // an empty 1/16 cell
  ui.tap(twill::kA);                   // add a 1/16 note at Q/4
  CHECK(ui.notes_at(Q / 4) == 1);
  const Tick fine = Q / 4 / 8;
  auto note = [&]() -> const twill::Note* {
    const twill::Note* found = nullptr;
    for (const twill::Note& x : ui.clip().notes)
      if (x.start >= Q / 4 && x.start < Q / 2) found = &x;
    return found;
  };
  // A + L + Left: an eighth of a step shorter.
  ui.step(twill::kA);
  ui.step(twill::kA | twill::kL);
  ui.step(twill::kA | twill::kL | twill::kLeft);
  ui.step(twill::kA | twill::kL);
  ui.step(twill::kL);
  ui.step(0);
  CHECK(note() && note()->length == Q / 4 - fine);
  // B + L + Right twice: starts two eighths later; the cursor stays on the grid and still finds it.
  ui.step(twill::kB);
  ui.step(twill::kB | twill::kL);
  ui.step(twill::kB | twill::kL | twill::kRight);
  ui.step(twill::kB | twill::kL);
  ui.step(twill::kB | twill::kL | twill::kRight);
  ui.step(twill::kB | twill::kL);
  ui.step(twill::kL);
  ui.step(0);
  CHECK(note() && note()->start == Q / 4 + 2 * fine);
  CHECK(ui.app.track() == 0);  // releasing L after a nudge doesn't switch tracks
  ui.tap(twill::kB);              // deletes it: the cursor's cell still finds the nudged note
  CHECK(!note());
}

void test_ui_roll_add_move_resize_delete_undo() {
  UiDriver ui;
  ui.tap(twill::kRight, twill::kSelect);  // to the piano roll (Kick track, cursor on its first note)
  CHECK(ui.app.view() == twill::App::View::PianoRoll);
  const size_t before = ui.clip().notes.size();
  const twill::Project original = ui.app.project();

  ui.tap(twill::kRight);  // one 1/16 step to an empty cell
  ui.tap(twill::kA);      // add
  CHECK(ui.clip().notes.size() == before + 1);
  CHECK(ui.notes_at(Q / 4) == 1);

  // A held + right: one step longer (one undo step for the whole hold).
  ui.step(twill::kA);
  ui.step(twill::kA | twill::kRight);
  ui.step(twill::kA);
  ui.step(0);
  const twill::Note* n = nullptr;
  for (const twill::Note& x : ui.clip().notes)
    if (x.start == Q / 4) n = &x;
  CHECK(n && n->length == Q / 2);

  // B held + right twice: moves two steps; B released after moving doesn't delete.
  ui.step(twill::kB);
  ui.step(twill::kB | twill::kRight);
  ui.step(twill::kB);
  ui.step(twill::kB | twill::kRight);
  ui.step(twill::kB);
  ui.step(0);
  CHECK(ui.notes_at(Q / 4) == 0);
  CHECK(ui.notes_at(3 * Q / 4) == 1);
  CHECK(ui.clip().notes.size() == before + 1);

  // B tap deletes it.
  ui.tap(twill::kB);
  CHECK(ui.clip().notes.size() == before);

  // Undo: delete, move (one step), resize, add.
  for (int i = 0; i < 4; ++i) ui.tap(twill::kB, twill::kSelect);
  // Same notes and tracks as before (note ids are never reused, so the next id stays advanced).
  bool same = ui.app.project().tracks == original.tracks;
  for (size_t i = 0; i < original.clips.size(); ++i)
    same = same && ui.app.project().clips[i].notes == original.clips[i].notes;
  CHECK(same);
  CHECK(!ui.app.history().can_undo());
}

void test_ui_rectangle_select_and_delete() {
  UiDriver ui;
  ui.tap(twill::kRight, twill::kSelect);
  const size_t before = ui.clip().notes.size();  // the Kick clip: beats 1-4 and an extra
  // Y held from the first note, across a whole beat and a half: selects the first two beats' notes.
  ui.step(twill::kY);
  for (int i = 0; i < 5; ++i) {
    ui.step(twill::kY | twill::kRight);
    ui.step(twill::kY);
  }
  ui.step(0);
  // Back to the first note and delete: the whole selection goes.
  for (int i = 0; i < 5; ++i) ui.tap(twill::kLeft);
  ui.tap(twill::kB);
  CHECK(ui.clip().notes.size() == before - 2);
}

void test_ui_arrangement_new_clip_and_undo() {
  UiDriver ui;
  CHECK(ui.app.view() == twill::App::View::Arrangement);
  ui.tap(twill::kDown);  // Snare, whose clip starts at bar 3: bar 1 is empty
  const size_t clips = ui.app.project().clips.size();
  const size_t placed = ui.app.project().tracks[1].arrangement.size();
  ui.tap(twill::kA);
  CHECK(ui.app.project().clips.size() == clips + 1);
  CHECK(ui.app.project().tracks[1].arrangement.size() == placed + 1);
  ui.tap(twill::kB, twill::kSelect);  // undo: one step removes both the clip and its placement
  CHECK(ui.app.project().clips.size() == clips);
  CHECK(ui.app.project().tracks[1].arrangement.size() == placed);
}

void test_ui_track_settings_engine_to_sampler() {
  UiDriver ui;
  ui.tap(twill::kLeft);  // Kick's header
  ui.tap(twill::kX);     // its settings, on Engine
  for (int i = 0; i < 3; ++i) ui.tap(twill::kRight);  // bass drum -> snare -> hi-hat -> sampler
  CHECK(ui.app.project().tracks[0].instrument == twill::InstrumentType::Sampler);
  CHECK(std::string(twill::App::sound_name(ui.app.project().tracks[0])) == "Sampler");
  CHECK(ui.app.inspector_open());
  ui.tap(twill::kDown);  // "Sound settings"
  ui.tap(twill::kA);     // goes to the Sound view, where the sample is chosen
  CHECK(!ui.app.inspector_open());
  CHECK(ui.app.view() == twill::App::View::Instrument);
  ui.tap(twill::kB, twill::kSelect);  // undo: a synth again
  CHECK(ui.app.project().tracks[0].instrument == twill::InstrumentType::Macro);
}

void test_ui_browser_up_a_folder() {
  UiDriver ui;
  ui.tap(twill::kLeft, twill::kSelect);  // arrangement -> mixer
  ui.tap(twill::kLeft, twill::kSelect);  // -> sound view
  for (int i = 0; i < 3; ++i) ui.tap(twill::kRight);  // engine -> sampler
  ui.tap(twill::kDown);  // mode
  ui.tap(twill::kDown);  // sample
  ui.tap(twill::kA);     // the browser, in samples/: [Up a folder], Factory/ (selected)
  ui.tap(twill::kA);     // into Factory/: [Up a folder], 36 Kick.wav (selected), ...
  ui.tap(twill::kUp);    // "Up a folder"
  ui.tap(twill::kA);     // back in samples/, Factory/ selected
  ui.tap(twill::kA);     // into Factory/ again
  ui.tap(twill::kA);     // pick 36 Kick
  CHECK(ui.app.project().tracks[0].sampler.sample == "samples/Factory/36 Kick.wav");
}

void test_ui_sound_view_kit() {
  UiDriver ui;
  CHECK(ui.platform.files.count("samples/Factory/36 Kick.wav") == 1);  // factory sounds were made
  {
    // An older version's file is replaced on the next start; an unchanged one is left alone.
    twill::NullPlatform pl;
    pl.write_file("samples/Factory/46 Open Hat.wav", std::vector<uint8_t>{1, 2, 3});
    CHECK(twill::make_factory_sounds(pl) > 1);
    CHECK(pl.files["samples/Factory/46 Open Hat.wav"].size() > 3);
    CHECK(twill::make_factory_sounds(pl) == 0);
  }
  ui.tap(twill::kLeft, twill::kSelect);  // arrangement -> mixer
  ui.tap(twill::kLeft, twill::kSelect);  // -> sound view
  CHECK(ui.app.view() == twill::App::View::Instrument);
  for (int i = 0; i < 3; ++i) ui.tap(twill::kRight);  // engine: bass drum -> snare -> hi-hat -> sampler
  ui.tap(twill::kDown);
  ui.tap(twill::kRight);
  ui.tap(twill::kRight);  // mode: single -> slices -> kit
  ui.tap(twill::kDown);
  ui.tap(twill::kA);      // load the factory kit
  const twill::Track& t = ui.app.project().tracks[0];
  CHECK(t.instrument == twill::InstrumentType::Sampler);
  CHECK(t.sampler.mode == twill::SamplerMode::Kit);
  CHECK(t.sampler.kit[0].sample == "samples/Factory/36 Kick.wav");
  CHECK(ui.app.load_sample(t.sampler.kit[0].sample) != nullptr);
  // With the kit's settings: General MIDI notes, hats in a choke group, the open hat decayed.
  CHECK(t.sampler.kit[38 - twill::kKitFirstNote].sample == "samples/Factory/38 Snare.wav");
  CHECK(t.sampler.kit[42 - twill::kKitFirstNote].choke == 1 && t.sampler.kit[46 - twill::kKitFirstNote].choke == 1);
  CHECK(t.sampler.kit[46 - twill::kKitFirstNote].decay < 1 && t.sampler.kit[51 - twill::kKitFirstNote].decay == 1);
  for (int i = 0; i < 6; ++i) ui.tap(twill::kB, twill::kSelect);  // undo all six changes
  CHECK(ui.app.project().tracks[0].instrument == twill::InstrumentType::Macro);
  CHECK(ui.app.project().tracks[0].sampler.kit[0].sample.empty());
}

// A sample the project stops using is freed once the engine has let go of it, and comes back when
// it's used again.
void test_ui_unused_samples_freed() {
  UiDriver ui;
  ui.audio = true;
  const std::string kick = "samples/Factory/36 Kick.wav", snare = "samples/Factory/38 Snare.wav";
  twill::Track t = ui.app.project().tracks[0];
  t.instrument = twill::InstrumentType::Sampler;
  t.sampler.sample = kick;
  ui.app.history().change_track(0, t);
  for (int i = 0; i < 5; ++i) ui.step(0);
  std::weak_ptr<const twill::SampleData> kick_data = ui.app.load_sample(kick);
  CHECK(!kick_data.expired());
  ui.app.audition(0, 60);  // a voice on it, which the change below must stop
  ui.step(0);
  t.sampler.sample = snare;
  ui.app.history().change_track(0, t);
  for (int i = 0; i < 80; ++i) ui.step(0);  // over a second
  CHECK(kick_data.expired());
  CHECK(ui.app.loaded_samples() >= 1);
  ui.tap(twill::kB, twill::kSelect);  // undo: the kick again, loaded again
  for (int i = 0; i < 5; ++i) ui.step(0);
  CHECK(ui.app.load_sample(kick) != nullptr);
  CHECK(ui.app.project().tracks[0].sampler.sample == kick);
}

void test_ui_lane_velocity_and_automation() {
  UiDriver ui;
  ui.tap(twill::kRight, twill::kSelect);  // roll, Kick clip, cursor on the first note
  ui.tap(twill::kDown, twill::kSelect);   // focus the lane strip (velocity)
  const int before = ui.clip().notes[0].velocity;
  ui.tap(twill::kUp);
  CHECK(ui.clip().notes[0].velocity == std::min(127, before + 4));
  ui.tap(twill::kB, twill::kSelect);
  CHECK(ui.clip().notes[0].velocity == before);
  // Holding Down repeats; the whole hold is one undo step.
  for (int i = 0; i < 30; ++i) ui.step(twill::kDown);
  ui.step(0);
  CHECK(ui.clip().notes[0].velocity < before - 4);
  ui.tap(twill::kB, twill::kSelect);
  CHECK(ui.clip().notes[0].velocity == before);

  // Switch the lane to Timbre in the clip panel, then raise it at the cursor: a point appears.
  ui.tap(twill::kUp, twill::kSelect);  // back to the notes
  ui.tap(twill::kRight);            // an empty cell, so X opens the clip panel
  ui.tap(twill::kX);
  CHECK(ui.app.inspector_open());
  for (int i = 0; i < 4; ++i) ui.tap(twill::kDown);  // from Grid past Key, Scale, Fold, to Lane
  ui.tap(twill::kRight);   // Velocity -> Harmonics
  ui.tap(twill::kRight);   // -> Timbre
  ui.tap(twill::kX);
  ui.tap(twill::kDown, twill::kSelect);
  ui.tap(twill::kUp);
  const twill::Lane* lane = ui.clip().lane(twill::kParamTimbre);
  CHECK(lane && lane->points.size() == 1);
  CHECK(lane && std::fabs(lane->points[0].value - 0.52f) < 1e-4f);
}

void test_ui_menu_opens_project() {
  UiDriver ui;
  ui.tap(twill::kRight, twill::kSelect);  // from the roll, say
  ui.tap(twill::kMenu);                // Menu alone: the settings panel (Controls, Theme, New song names, Tempo, ...)
  CHECK(ui.app.inspector_open());
  const float tempo = ui.app.project().tempo;
  for (int i = 0; i < 3; ++i) ui.tap(twill::kDown);
  ui.tap(twill::kRight);
  CHECK(ui.app.project().tempo == tempo + 1);
  ui.tap(twill::kB);  // close
  CHECK(!ui.app.inspector_open());
  // A second tap of Menu puts the panel away, as does B.
  ui.tap(twill::kMenu);
  CHECK(ui.app.inspector_open());
  ui.tap(twill::kMenu);
  CHECK(!ui.app.inspector_open());
  // About, the last row: version and build.
  ui.tap(twill::kMenu);
  ui.tap(twill::kUp);  // wraps to the last row
  ui.tap(twill::kA);
  CHECK_MSG(ui.app.inspector_title() == "About Twill Studio", "panel \"%s\"", ui.app.inspector_title().c_str());
  ui.tap(twill::kMenu);  // from the About panel too
  CHECK(!ui.app.inspector_open());
}

// Select all in the roll: Y + A, or the clip panel's row.
void test_ui_select_all() {
  UiDriver ui;
  ui.tap(twill::kRight, twill::kSelect);  // the roll, on the demo's first clip
  const size_t notes = ui.clip().notes.size();
  CHECK(notes > 1);
  // Y + A selects them all, without adding a note; B then deletes them all.
  ui.step(twill::kY);
  ui.step(twill::kY | twill::kA);
  ui.step(twill::kY);
  ui.step(0);
  CHECK(ui.clip().notes.size() == notes);
  ui.tap(twill::kB);
  CHECK_MSG(ui.clip().notes.empty(), "%zu of %zu notes left", ui.clip().notes.size(), notes);
  ui.tap(twill::kB, twill::kSelect);  // undo
  CHECK(ui.clip().notes.size() == notes);
}

void test_ui_dark_mode_setting() {
  UiDriver ui;
  CHECK(!ui.app.dark_mode());
  ui.tap(twill::kMenu);   // settings, on Controls
  ui.tap(twill::kDown);   // Theme
  ui.tap(twill::kRight);  // dark
  CHECK(ui.app.dark_mode());
  CHECK(ui.platform.files.count("settings.txt") == 1);
  ui.tap(twill::kB);
  // A new start of the app reads it back.
  twill::App again(ui.platform);
  again.start();
  CHECK(again.dark_mode());
  again.stop();
}

void test_ui_move_and_clear_track() {
  UiDriver ui;
  ui.tap(twill::kLeft);  // Kick's header
  ui.tap(twill::kDown);  // Snare's
  CHECK(ui.app.track() == 1);
  ui.tap(twill::kX);     // Snare's settings, on Engine
  ui.tap(twill::kUp);    // Name,
  ui.tap(twill::kUp);    // then from the top round to the last rows: Clear track,
  ui.tap(twill::kUp);    // then Position
  const uint32_t colour = ui.app.project().tracks[1].colour;
  ui.tap(twill::kRight);  // one place down
  CHECK(ui.app.project().tracks[2].name == "Snare");
  CHECK(ui.app.project().tracks[1].name == "Hats");
  CHECK(ui.app.track() == 2);
  CHECK(ui.app.inspector_open());
  ui.tap(twill::kRight, twill::kR);  // to the end
  const int last = int(ui.app.project().tracks.size()) - 1;
  CHECK(ui.app.project().tracks[size_t(last)].name == "Snare");
  CHECK(ui.app.project().tracks[size_t(last)].colour == colour);  // its colour goes with it
  CHECK(ui.app.track() == last);
  ui.tap(twill::kDown);  // Clear track
  ui.tap(twill::kA);
  const twill::Track& t = ui.app.project().tracks[size_t(last)];
  CHECK(t.arrangement.empty());
  CHECK(t.name == "Synth");
  CHECK(t.colour == colour);
  // Clearing is one undo step, and so is each move.
  CHECK(ui.app.history().undo());
  CHECK(ui.app.project().tracks[size_t(last)].name == "Snare");
  CHECK(!ui.app.project().tracks[size_t(last)].arrangement.empty());
  CHECK(ui.app.history().undo());
  CHECK(ui.app.history().undo());
  CHECK(ui.app.project().tracks[1].name == "Snare");
  CHECK(ui.app.project().tracks[2].name == "Hats");
}

void test_ui_add_note_on_midi_out_track() {
  // Adding a note auditions it; on a MIDI Out track that is a MIDI note, not a synth voice.
  UiDriver ui;
  ui.audio = true;
  for (int i = 0; i < 40 && ui.app.project().tracks[0].instrument != twill::InstrumentType::MidiOut; ++i)
    ui.app.step_sound(0, 1);  // Kick: on through the list to MIDI out, the last sound
  CHECK(ui.app.project().tracks[0].instrument == twill::InstrumentType::MidiOut);
  ui.tap(twill::kX);  // open Kick's clip in the roll
  CHECK(ui.app.open_clip() != nullptr);
  const size_t before = ui.app.open_clip()->notes.size();
  ui.tap(twill::kRight);
  ui.tap(twill::kA);
  for (int i = 0; i < 60; ++i) ui.step(0);  // a second of audio with the audition sounding
  CHECK(ui.app.open_clip()->notes.size() == before + 1);
  // The audition went to the MIDI device: a note on, then its note off.
  int ons = 0, offs = 0;
  for (const twill::MidiMessage& m : ui.platform.fake_midi.sent) {
    ons += (m.status & 0xF0) == 0x90 && m.data2 > 0;
    offs += (m.status & 0xF0) == 0x80 || ((m.status & 0xF0) == 0x90 && m.data2 == 0);
  }
  CHECK(ons == 1);
  CHECK(offs == 1);
}

void test_ui_keyboard_into_midi_out_track() {
  // Notes from a device, played into a MIDI Out track, go out on the track's channel to the other
  // devices, never back to the one they came from; the engine plays nothing.
  UiDriver ui;
  for (int i = 0; i < 40 && ui.app.project().tracks[0].instrument != twill::InstrumentType::MidiOut; ++i)
    ui.app.step_sound(0, 1);
  twill::Track t = ui.app.project().tracks[0];
  t.midi_channel = 3;
  ui.app.history().change_track(0, t);
  ui.step(0);
  auto& midi = ui.platform.fake_midi;
  midi.sent.clear();
  midi.sent_except.clear();
  twill::MidiMessage on{0x90, 64, 100};
  on.source = 7;
  midi.in.push_back(on);
  ui.step(0);
  CHECK(midi.sent.size() == 1);
  CHECK(midi.sent[0].status == 0x93 && midi.sent[0].data1 == 64 && midi.sent[0].data2 == 100);
  CHECK(midi.sent_except[0] == 7);
  ui.app.set_track(1);  // the note off still goes where the note on went
  twill::MidiMessage off{0x80, 64, 0};
  off.source = 7;
  midi.in.push_back(off);
  ui.step(0);
  CHECK(midi.sent.size() == 2);
  CHECK(midi.sent[1].status == 0x83 && midi.sent[1].data1 == 64);
  CHECK(midi.sent_except[1] == 7);
}

// Made-up song names: two words, varied, fit for the name editor; X there makes up another; the
// setting can turn them off.
void test_ui_song_names() {
  std::set<std::string> seen;
  for (uint32_t i = 0; i < 200; ++i) {
    const std::string n = twill::song_name(i);
    seen.insert(n);
    const size_t space = n.find(' ');
    CHECK(space != std::string::npos && n.find(' ', space + 1) == std::string::npos);
    for (char c : n) CHECK(std::isalpha(static_cast<unsigned char>(c)) || c == ' ');
  }
  CHECK_MSG(seen.size() > 190, "%zu different names in 200", seen.size());
  CHECK(twill::song_name(42) == twill::song_name(42));

  UiDriver ui;
  ui.tap(twill::kDown);  // Snare row, bar 1 empty
  ui.tap(twill::kX);     // settings panel; Name is the seventh row
  for (int i = 0; i < 6; ++i) ui.tap(twill::kDown);
  ui.tap(twill::kA);  // the name editor
  ui.tap(twill::kX);  // make one up
  ui.tap(twill::kA);
  CHECK_MSG(ui.app.project().name != "Demo" && ui.app.project().name.find(' ') != std::string::npos, "name \"%s\"",
            ui.app.project().name.c_str());
  ui.app.set_auto_names(false);
  ui.app.new_project();
  CHECK(ui.app.project().name == "Untitled");
  CHECK(ui.platform.files.count("settings.txt") == 1);
}

// L2 / R2 step through the views; a tap of L or R on its own changes nothing; Select + Up / Down
// choose the track in the sound view; Select + X opens and closes the settings panel.
void test_ui_shoulder_buttons() {
  using V = twill::App::View;
  UiDriver ui;
  CHECK(ui.app.view() == V::Arrangement);
  ui.tap(twill::kR2);
  CHECK(ui.app.view() == V::PianoRoll);
  const int track = ui.app.track();
  ui.tap(twill::kR);
  ui.tap(twill::kL);
  CHECK(ui.app.track() == track);  // L1 / R1 taps no longer change track
  CHECK(ui.app.view() == V::PianoRoll);
  ui.tap(twill::kR2);
  CHECK(ui.app.view() == V::Instrument);
  ui.tap(twill::kDown, twill::kSelect);
  CHECK(ui.app.track() == track + 1);
  ui.tap(twill::kUp, twill::kSelect);
  CHECK(ui.app.track() == track);
  ui.tap(twill::kR2);
  CHECK(ui.app.view() == V::Mixer);
  ui.tap(twill::kR2);
  CHECK(ui.app.view() == V::Arrangement);  // wraps round
  ui.tap(twill::kL2);
  CHECK(ui.app.view() == V::Mixer);
  ui.tap(twill::kX, twill::kSelect);
  CHECK(ui.app.inspector_open());
  ui.tap(twill::kX, twill::kSelect);
  CHECK(!ui.app.inspector_open());
}

void test_ui_x_and_y_in_sound_and_mixer() {
  // X in the sound view opens the track's settings; Y in the mixer plays the track's sound.
  using V = twill::App::View;
  UiDriver ui;
  ui.app.show(V::Instrument);
  ui.tap(twill::kX);
  CHECK(ui.app.inspector_open());
  ui.tap(twill::kX);
  CHECK(!ui.app.inspector_open());
  ui.app.show(V::Mixer);
  ui.audio = true;
  for (int i = 0; i < 4; ++i) ui.step(0);
  CHECK(ui.app.engine().active_voices() == 0);
  ui.tap(twill::kY);
  CHECK(ui.app.engine().active_voices() > 0);
}

void test_ui_playhead_redraws_cleanly() {
  // While playing, only the playhead's strip is redrawn each frame; that must leave the screen as a
  // full redraw would (the marker in the ruler once left a trail).
  using V = twill::App::View;
  for (V v : {V::Arrangement, V::PianoRoll}) {
    UiDriver ui;
    ui.app.show(v);
    ui.audio = true;
    ui.tap(twill::kStart);
    for (int i = 0; i < 90; ++i) ui.step(0);
    ui.audio = false;  // the playhead stays where it is
    ui.step(0);
    std::vector<uint32_t> partial(ui.canvas.pixels(), ui.canvas.pixels() + 1024 * 768);
    ui.app.invalidate_all();
    ui.step(0);
    std::vector<uint32_t> full(ui.canvas.pixels(), ui.canvas.pixels() + 1024 * 768);
    CHECK(partial == full);
  }
}

void test_ui_play_from_cursor() {
  // L or R + Start plays from the cursor, and the shoulder button's release doesn't change track.
  // Select + Start does nothing: it's left to the handhelds' "close the app" shortcut.
  UiDriver ui;
  ui.audio = true;
  ui.tap(twill::kRight, twill::kSelect);  // the roll
  for (int i = 0; i < 4; ++i) ui.tap(twill::kRight);  // a beat in
  const int track = ui.app.track();
  ui.tap(twill::kStart, twill::kR);
  ui.step(0);
  CHECK(ui.app.engine().playing());
  CHECK_MSG(ui.app.engine().position() >= Q, "played from %d", int(ui.app.engine().position()));
  CHECK(ui.app.track() == track);
  ui.tap(twill::kStart);  // stop
  CHECK(!ui.app.engine().playing());
  ui.tap(twill::kStart, twill::kSelect);
  ui.step(0);
  CHECK(!ui.app.engine().playing());
}

void test_keep_awake() {
  // The device is kept from dimming or sleeping while a song plays, and for five minutes after a
  // MIDI keyboard was last played; otherwise it's left to save its battery.
  UiDriver ui;
  ui.audio = true;
  CHECK(!ui.platform.awake);
  ui.tap(twill::kStart);
  ui.step(0);
  CHECK(ui.app.engine().playing() && ui.platform.awake);
  ui.tap(twill::kStart);  // stop
  ui.step(0);
  CHECK(!ui.platform.awake);
  ui.platform.fake_midi.in.push_back({0x90, 60, 100});
  ui.step(0);
  CHECK(ui.platform.awake);
  ui.platform.fake_midi.in.push_back({0x80, 60, 0});
  ui.step(0);
  ui.platform.advance_ms(301000);
  ui.step(0);
  CHECK(!ui.platform.awake);
  ui.tap(twill::kStart);
  ui.step(0);
  CHECK(ui.platform.awake);
  ui.app.stop();
  CHECK(!ui.platform.awake);
}

void test_ui_project_tempo() {
  UiDriver ui;
  ui.tap(twill::kDown);  // Snare: bar 1 is empty
  ui.tap(twill::kX);     // settings panel: Controls, Theme, New song names, Tempo, ...
  CHECK(ui.app.inspector_open());
  const float tempo = ui.app.project().tempo;
  for (int i = 0; i < 3; ++i) ui.tap(twill::kDown);
  ui.tap(twill::kRight);
  ui.tap(twill::kRight, twill::kR);
  CHECK(ui.app.project().tempo == tempo + 11);
}

// With the ADSR as the amplifier, the FM, timbre and morph amounts follow the ADSR, not the
// internal decay envelope: Decay changes nothing, and the ADSR's level does.
void test_adsr_drives_modulation_amounts() {
  auto render = [](float decay, float envelope) {
    twill::PlaitsVoice::seed_random(1);
    twill::PlaitsVoice v;
    twill::MacroParams p;
    p.engine = 8;
    p.note = 48;
    p.decay = decay;
    p.timbre_amount = p.morph_amount = 0.8f;
    p.fm_amount = 0.4f;
    p.use_lpg = false;
    p.envelope = envelope;
    std::vector<float> out(24000), aux(24000);
    v.note_on();
    v.render(p, out.data(), aux.data(), int(out.size()));
    return out;
  };
  CHECK(render(0.1f, 0.7f) == render(0.9f, 0.7f));
  CHECK(render(0.5f, 0.2f) != render(0.5f, 0.9f));
}

// The audio load: measured around every callback, as a share of the time its block lasts.
void test_audio_load() {
  twill::NullPlatform platform(1024, 768, 2);
  twill::App app(platform);
  app.start();
  CHECK(app.engine().load_highest() == 0);
  std::vector<float> buf(2 * 512);
  for (int i = 0; i < 20; ++i) app.engine().render(buf.data(), 512);
  const float avg = app.engine().load_average(), peak = app.engine().take_load_peak();
  CHECK(avg > 0 && peak > 0 && peak <= app.engine().load_highest());
  CHECK(app.engine().take_load_peak() == 0);  // taking it starts it again
  CHECK(app.load_text().find("%, peaks ") != std::string::npos);
  app.stop();
}

void test_ui_track_names() {
  UiDriver ui;
  ui.tap(twill::kLeft);  // Kick's header
  ui.tap(twill::kX);     // its settings, on Engine
  ui.tap(twill::kUp);    // Name
  ui.tap(twill::kRight);  // the next part name after Kick
  CHECK(ui.app.project().tracks[0].name == "Snare");
  ui.tap(twill::kB, twill::kSelect);  // a rename is undoable
  CHECK(ui.app.project().tracks[0].name == "Kick");
  ui.tap(twill::kA);  // spell one out
  CHECK(!ui.app.inspector_open());
  ui.tap(twill::kY);  // the cursor is past the end: nothing to delete
  ui.tap(twill::kLeft);
  ui.tap(twill::kY);  // "Kic"
  ui.tap(twill::kA);  // done: back in the panel
  CHECK(ui.app.project().tracks[0].name == "Kic");
  CHECK(ui.app.inspector_open());
  ui.tap(twill::kA);
  ui.tap(twill::kX);  // a part name in place of the draft
  ui.tap(twill::kB);  // cancel keeps the old name
  CHECK(ui.app.project().tracks[0].name == "Kic");
  ui.tap(twill::kA);
  ui.tap(twill::kX);  // "Kic" isn't a part name, so the list starts again: Drums
  ui.tap(twill::kA);
  CHECK(ui.app.project().tracks[0].name == "Drums");
  // Part names match whatever their case, and the list goes round.
  CHECK(twill::App::next_role_name("bass", 1) == "Sub");
  CHECK(twill::App::next_role_name("Drums", -1) == "FX");
}

void test_ui_inspector_scrolls() {
  UiDriver ui;
  ui.tap(twill::kLeft);  // Kick's header
  ui.tap(twill::kX);     // its settings: more fields than fit at 1024 x 768
  CHECK(ui.app.inspector_open());
  CHECK(ui.app.inspector_scroll() == 0);
  ui.tap(twill::kUp);    // Name, the first field
  ui.tap(twill::kUp);    // wraps to the last field, which scrolls into view
  CHECK(ui.app.inspector_scroll() > 0);
  ui.tap(twill::kDown);  // back to the first
  CHECK(ui.app.inspector_scroll() == 0);
}

void test_ui_mixer() {
  UiDriver ui;
  ui.tap(twill::kLeft, twill::kSelect);  // arrangement -> mixer
  CHECK(ui.app.view() == twill::App::View::Mixer);
  for (int i = 0; i < 4; ++i) ui.tap(twill::kDown);  // Kick's reverb send
  ui.step(twill::kA);
  ui.step(twill::kA | twill::kUp);
  ui.step(twill::kA);
  ui.step(twill::kA | twill::kUp);
  ui.step(0);
  CHECK(ui.app.project().tracks[0].send_reverb > 0.03f);
  ui.tap(twill::kB);  // reset
  CHECK(ui.app.project().tracks[0].send_reverb == 0);
  ui.tap(twill::kDown);
  ui.tap(twill::kA);  // mute
  CHECK(ui.app.project().tracks[0].mute);
  // The master strip's EQ, then the effects panel.
  ui.tap(twill::kLeft);  // wraps to the master strip; the cursor lands on its last row, EQ high
  ui.tap(twill::kUp, twill::kA);
  CHECK(ui.app.project().mix.eq_high > 0);
  ui.tap(twill::kX);
  CHECK(ui.app.inspector_open());
}

void test_ui_project_files() {
  UiDriver ui;
  ui.tap(twill::kDown);  // Snare row, bar 1 empty
  ui.tap(twill::kX);     // settings panel: Controls, Theme, New song names, Tempo, Key, Scale, Name, Save, Load, New, Render
  for (int i = 0; i < 6; ++i) ui.tap(twill::kDown);
  // Rename: the first letter "D" of "Demo" up by one, to "E".
  ui.tap(twill::kA);
  for (int i = 0; i < 4; ++i) ui.tap(twill::kLeft);
  ui.tap(twill::kUp);
  ui.tap(twill::kA);
  CHECK(ui.app.project().name == "Eemo");
  // Save.
  ui.tap(twill::kX);
  for (int i = 0; i < 7; ++i) ui.tap(twill::kDown);
  ui.tap(twill::kA);
  CHECK(ui.platform.files.count("projects/Eemo.twill") == 1);
  CHECK(!ui.app.has_unsaved_changes());
  const twill::Project saved = ui.app.project();

  // Change something, then New: the first press only warns.
  ui.app.history().set_tempo(150);
  CHECK(ui.app.has_unsaved_changes());
  ui.tap(twill::kDown);  // Load
  ui.tap(twill::kDown);  // New project
  ui.tap(twill::kA);
  CHECK(ui.app.project().tempo == 150);
  ui.tap(twill::kA);  // second press: discard and start anew
  // A made-up two-word name.
  const std::string fresh = ui.app.project().name;
  CHECK_MSG(fresh.find(' ') != std::string::npos && fresh != "Untitled" && ui.app.project().clips.empty(),
            "new project \"%s\"", fresh.c_str());

  // Load the saved one back.
  CHECK(ui.app.load_project_file("Eemo"));
  CHECK(ui.app.project() == saved);
  CHECK(!ui.app.history().can_undo());

  // Render: the song (8 bars at 112 BPM) plus 3 s of tail.
  CHECK(ui.app.render_song_to_wav());
  auto it = ui.platform.files.find("renders/Eemo.wav");
  CHECK(it != ui.platform.files.end());
  if (it != ui.platform.files.end()) {
    const double seconds = (it->second.size() - 44) / 4.0 / 48000;
    CHECK_MSG(std::fabs(seconds - (32 * 60.0 / 112 + 3)) < 0.01, "render is %.2f s", seconds);
  }

  // Stems: one 32-bit float WAV per track that plays, all as long as the song's render. A muted
  // track has none.
  auto& tracks = ui.app.project().tracks;
  tracks[1].mute = true;
  int expected = 0;
  for (const twill::Track& t : tracks) expected += !t.mute && !t.arrangement.empty();
  CHECK(ui.app.render_stems() == expected);
  int found = 0;
  for (const auto& f : ui.platform.files) {
    if (f.first.rfind("renders/Eemo stems/", 0) != 0) continue;
    ++found;
    const std::vector<uint8_t>& b = f.second;
    CHECK(b.size() > 58 && b[20] == 3 && b[34] == 32);  // IEEE float, 32 bits
    const double seconds = (b.size() - 58) / 8.0 / 48000;
    CHECK_MSG(std::fabs(seconds - (32 * 60.0 / 112 + 3)) < 0.01, "stem %s is %.2f s", f.first.c_str(), seconds);
  }
  CHECK(found == expected && expected > 0);
  CHECK(ui.platform.files.count("renders/Eemo stems/01 " + tracks[0].name + ".wav") == 1);
  CHECK(ui.platform.files.count("renders/Eemo stems/02 " + tracks[1].name + ".wav") == 0);
}

void test_ui_midi_record_and_clock() {
  UiDriver ui;
  ui.audio = true;
  ui.app.project().midi_clock = twill::MidiClock::Send;
  ui.tap(twill::kRight, twill::kSelect);  // roll: the Kick clip, one bar, placed for 8 bars
  ui.app.toggle_recording();              // arm recording (the settings panel's "Record from MIDI")
  ui.tap(twill::kStart);               // play (looping the clip's placement)
  // About half a second in (beat 2 at 112 BPM is 0.54 s), play a note for about a quarter second.
  for (int i = 0; i < 33; ++i) ui.step(0);
  ui.platform.fake_midi.in.push_back(twill::MidiMessage{0x90, 72, 100});
  for (int i = 0; i < 16; ++i) ui.step(0);
  ui.platform.fake_midi.in.push_back(twill::MidiMessage{0x80, 72, 0});
  ui.step(0);
  const twill::Note* rec = nullptr;
  for (const twill::Note& n : ui.clip().notes)
    if (n.pitch == 72) rec = &n;
  CHECK(rec != nullptr);
  if (rec) {
    CHECK_MSG(rec->start == Q, "recorded at %d (beat 2 is %d)", rec->start, Q);  // snapped to 1/16
    CHECK_MSG(rec->length >= Q / 4 && rec->length <= Q, "length %d", rec->length);
  }
  // Clock out: a start, then 24 clocks a beat.
  const auto& sent = ui.platform.fake_midi.sent;
  CHECK(!sent.empty() && sent[0].status == 0xFA);
  const long clocks = std::count_if(sent.begin(), sent.end(), [](const twill::MidiMessage& m) { return m.status == 0xF8; });
  const double beats = 50 * 0.016 * 112 / 60.0;
  CHECK_MSG(std::fabs(clocks - beats * 24) < 6, "%ld clocks for %.2f beats", clocks, beats);
}

// Following an external clock at 100 BPM (the demo song is at 112): start waits for the first
// pulse, the engine settles in step with the master, and stop stops it.
void test_ui_follow_midi_clock() {
  UiDriver ui;
  ui.audio = true;
  ui.app.project().midi_clock = twill::MidiClock::Follow;
  auto& in = ui.platform.fake_midi.in;
  const int64_t pulse_us = 25000;  // 100 BPM
  int64_t next = int64_t(ui.platform.micros()) + 100000;
  auto run_until = [&](int64_t t) {
    while (int64_t(ui.platform.micros()) < t) {
      while (next <= int64_t(ui.platform.micros())) {
        twill::MidiMessage clock{0xF8, 0, 0};
        clock.time_us = next;
        in.push_back(clock);
        next += pulse_us;
      }
      ui.step(0);
    }
  };
  run_until(next + 300000);  // clock while stopped: the tempo, but no playing
  CHECK(!ui.app.engine().playing());
  twill::MidiMessage start{0xFA, 0, 0};
  start.time_us = next - 1000;
  in.push_back(start);
  const int64_t first = next;  // the master's beat 0
  run_until(first + 4000000);  // four seconds
  CHECK(ui.app.engine().playing());
  // As heard (after the output latency), the engine's beats match the master's within ~10 ms.
  const double latency_beats = (2 * 256 + twill::Engine::kOutputDelay) / 48000.0 * 100 / 60;
  const double master = double(int64_t(ui.platform.micros()) - first) / pulse_us / 24;
  const double engine = ui.app.engine().elapsed_ticks() / Q - latency_beats;
  CHECK_MSG(std::fabs(engine - master) < 0.02, "engine at %.3f beats, master at %.3f", engine, master);
  twill::MidiMessage stop{0xFC, 0, 0};
  stop.time_us = int64_t(ui.platform.micros());
  in.push_back(stop);
  ui.step(0);
  ui.step(0);
  CHECK(!ui.app.engine().playing());
  // Off again: back to the project's tempo.
  ui.app.project().midi_clock = twill::MidiClock::Off;
  ui.step(0);
}

void test_ui_autosave_and_reload() {
  UiDriver ui;
  ui.tap(twill::kRight, twill::kSelect);
  ui.tap(twill::kRight);
  ui.tap(twill::kA);
  for (int i = 0; i < 200; ++i) ui.step(0);  // a few seconds pass
  CHECK(ui.platform.files.count(twill::App::kAutosaveName) == 1);

  twill::App second(ui.platform);  // a new launch, same files
  second.start();
  CHECK(second.project() == ui.app.project());
  second.stop();
}

struct TestCase {
  const char* name;
  void (*fn)();
};

}  // namespace

int main(int argc, char** argv) {
  const char* filter = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--update-golden") == 0) g_update_golden = true;
    else if (std::strcmp(argv[i], "--golden-dir") == 0 && i + 1 < argc) g_golden_dir = argv[++i];
    else filter = argv[i];
  }
  const TestCase tests[] = {
      {"timing_at_120", test_timing_at_120},
      {"timing_at_odd_tempo", test_timing_at_odd_tempo},
      {"loop_region", test_loop_region},
      {"placement_loops_its_clip", test_placement_loops_its_clip},
      {"placement_offset", test_placement_offset},
      {"probability", test_probability},
      {"parameter_locks", test_parameter_locks},
      {"mute_and_solo", test_mute_and_solo},
      {"track_limit", test_track_limit},
      {"sound_is_produced", test_sound_is_produced},
      {"file_round_trip", test_file_round_trip},
      {"file_skips_unknown_data", test_file_skips_unknown_data},
      {"file_rejects_damage", test_file_rejects_damage},
      {"undo_redo", test_undo_redo},
      {"snapshot_handoff_under_load", test_snapshot_handoff_under_load},
      {"same_output_at_any_block_size", test_same_output_at_any_block_size},
      {"touching_notes_retrigger_unless_legato", test_touching_notes_retrigger_unless_legato},
      {"voices_stolen_counted", test_voices_stolen_counted},
      {"master_compressor", test_master_compressor},
      {"master_limiter", test_master_limiter},
      {"golden_render", test_golden_render},
      {"sample_decode_and_resample", test_sample_decode_and_resample},
      {"sampler_pitch_follows_note", test_sampler_pitch_follows_note},
      {"sampler_slices_and_kit", test_sampler_slices_and_kit},
      {"sampler_loop_sustains", test_sampler_loop_sustains},
      {"kit_voices_per_drum", test_kit_voices_per_drum},
      {"kit_one_shot", test_kit_one_shot},
      {"sampler_transient_slices", test_sampler_transient_slices},
      {"filter_darkens", test_filter_darkens},
      {"modulators", test_modulators},
      {"legato_keeps_modulators", test_legato_keeps_modulators},
      {"lfo_phase_modes", test_lfo_phase_modes},
      {"automation_lane", test_automation_lane},
      {"send_effects", test_send_effects},
      {"effect_automation", test_effect_automation},
      {"midi_parser", test_midi_parser},
      {"midi_out_track", test_midi_out_track},
      {"midi_clock_out", test_midi_clock_out},
      {"midi_out_cc", test_midi_out_cc},
      {"clock_follower", test_clock_follower},
      {"midi_sender_timing", test_midi_sender_timing},
      {"midi_sender_thread", test_midi_sender_thread},
      {"live_notes", test_live_notes},
      {"audio_load", test_audio_load},
      {"keep_awake", test_keep_awake},
      {"adsr_drives_modulation_amounts", test_adsr_drives_modulation_amounts},
      {"ui_roll_add_move_resize_delete_undo", test_ui_roll_add_move_resize_delete_undo},
      {"ui_roll_fine_nudge", test_ui_roll_fine_nudge},
      {"ui_copy_paste", test_ui_copy_paste},
      {"ui_note_panel_start_length_snap", test_ui_note_panel_start_length_snap},
      {"ui_rectangle_select_and_delete", test_ui_rectangle_select_and_delete},
      {"ui_arrangement_new_clip_and_undo", test_ui_arrangement_new_clip_and_undo},
      {"ui_sound_view_kit", test_ui_sound_view_kit},
      {"ui_unused_samples_freed", test_ui_unused_samples_freed},
      {"ui_browser_up_a_folder", test_ui_browser_up_a_folder},
      {"ui_track_settings_engine_to_sampler", test_ui_track_settings_engine_to_sampler},
      {"ui_lane_velocity_and_automation", test_ui_lane_velocity_and_automation},
      {"ui_project_tempo", test_ui_project_tempo},
      {"ui_shoulder_buttons", test_ui_shoulder_buttons},
      {"ui_play_from_cursor", test_ui_play_from_cursor},
      {"ui_playhead_redraws_cleanly", test_ui_playhead_redraws_cleanly},
      {"ui_x_and_y_in_sound_and_mixer", test_ui_x_and_y_in_sound_and_mixer},
      {"ui_select_all", test_ui_select_all},
      {"ui_song_names", test_ui_song_names},
      {"ui_menu_opens_project", test_ui_menu_opens_project},
      {"ui_dark_mode_setting", test_ui_dark_mode_setting},
      {"ui_move_and_clear_track", test_ui_move_and_clear_track},
      {"ui_track_names", test_ui_track_names},
      {"ui_add_note_on_midi_out_track", test_ui_add_note_on_midi_out_track},
      {"ui_keyboard_into_midi_out_track", test_ui_keyboard_into_midi_out_track},
      {"ui_inspector_scrolls", test_ui_inspector_scrolls},
      {"ui_mixer", test_ui_mixer},
      {"ui_project_files", test_ui_project_files},
      {"ui_midi_record_and_clock", test_ui_midi_record_and_clock},
      {"ui_follow_midi_clock", test_ui_follow_midi_clock},
      {"ui_autosave_and_reload", test_ui_autosave_and_reload},
  };
  int run = 0;
  for (const TestCase& t : tests) {
    if (filter && !std::strstr(t.name, filter)) continue;
    int before = g_failures;
    t.fn();
    std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", t.name);
    ++run;
  }
  std::printf("%d tests, %d failed checks\n", run, g_failures);
  return g_failures ? 1 : 0;
}
