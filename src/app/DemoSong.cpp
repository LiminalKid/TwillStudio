#include "app/DemoSong.h"

#include <initializer_list>

namespace twill {

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

Project make_demo_song() {
  Project p;
  p.name = "Demo";
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

}  // namespace twill
