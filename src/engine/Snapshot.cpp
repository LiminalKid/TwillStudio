#include "engine/Snapshot.h"

#include <algorithm>

#include "engine/Sample.h"

namespace twill {

void SnapSampler::set(ParamId id, float v) {
  SamplerSettings s;
  s.mode = mode, s.root = root, s.fine = fine, s.start = start, s.end = end, s.loop = loop;
  s.loop_start = loop_start, s.loop_end = loop_end, s.reverse = reverse, s.slices = slices;
  s.slice_transients = slice_transients;
  s.set(id, v);
  mode = s.mode, root = s.root, fine = s.fine, start = s.start, end = s.end, loop = s.loop;
  loop_start = s.loop_start, loop_end = s.loop_end, reverse = s.reverse, slices = s.slices;
}

std::unique_ptr<Snapshot> build_snapshot(const Project& p, int max_tracks, uint64_t revision,
                                         const SampleResolver& samples) {
  auto s = std::make_unique<Snapshot>();
  s->revision = revision;
  s->tempo = p.tempo;
  s->midi_clock_out = p.midi_clock == MidiClock::Send;
  s->loop = p.loop && p.loop_end > p.loop_start;
  s->loop_start = p.loop_start;
  s->loop_end = p.loop_end;
  s->mix = p.mix;

  s->clips.reserve(p.clips.size());  // placements keep pointers into this vector
  for (const Clip& c : p.clips) {
    SnapClip sc;
    sc.length = std::max<Tick>(1, c.length);
    for (const Note& n : c.notes)
      if (n.start >= 0 && n.start < c.length && n.length > 0) sc.notes.push_back(n);
    std::stable_sort(sc.notes.begin(), sc.notes.end(),
                     [](const Note& a, const Note& b) { return a.start < b.start; });
    for (const Lane& l : c.lanes)
      if (!l.points.empty()) sc.lanes.push_back(l);
    s->clips.push_back(std::move(sc));
  }
  auto snap_clip = [&](uint32_t id) -> const SnapClip* {
    for (size_t i = 0; i < p.clips.size(); ++i)
      if (p.clips[i].id == id) return &s->clips[i];
    return nullptr;
  };

  const int n = std::min<int>(int(p.tracks.size()), max_tracks);
  bool any_solo = false;
  for (int i = 0; i < n; ++i) any_solo |= p.tracks[i].solo;
  for (int i = 0; i < n; ++i) {
    const Track& t = p.tracks[i];
    SnapTrack st;
    st.instrument = t.instrument;
    st.voices = std::max(1, std::min(t.voices, kMaxVoicesPerTrack));
    st.legato = t.voices == 0;
    st.macro = t.macro;
    st.tone = t.tone;
    const SamplerSettings& sm = t.sampler;
    st.sampler.mode = sm.mode;
    st.sampler.root = sm.root;
    st.sampler.fine = sm.fine;
    st.sampler.start = sm.start;
    st.sampler.end = sm.end;
    st.sampler.loop = sm.loop;
    st.sampler.loop_start = sm.loop_start;
    st.sampler.loop_end = sm.loop_end;
    st.sampler.reverse = sm.reverse;
    st.sampler.slices = sm.slices;
    st.sampler.slice_transients = sm.slice_transients;
    for (int k = 0; k < kKitSlots; ++k)
      st.sampler.kit[k] = SnapSampler::Slot{sm.kit[k].tune, sm.kit[k].level, sm.kit[k].pan, sm.kit[k].choke};
    if (samples && t.instrument == InstrumentType::Sampler) {
      if (!sm.sample.empty()) st.sample = samples(sm.sample);
      if (st.sample && sm.mode == SamplerMode::Slice && sm.slice_transients)
        st.slice_points = transient_slices(*st.sample, sm.start, sm.end, sm.slices);
      for (int k = 0; k < kKitSlots; ++k)
        if (!sm.kit[k].sample.empty()) st.kit[k] = samples(sm.kit[k].sample);
    }
    st.level = t.level;
    st.pan = t.pan;
    st.send_chorus = t.send_chorus;
    st.send_delay = t.send_delay;
    st.send_reverb = t.send_reverb;
    for (int m = 0; m < kModulators; ++m) st.mods[m] = t.mods[m];
    st.midi_channel = t.midi_channel;
    st.midi_cc[0] = t.midi_cc[0];
    st.midi_cc[1] = t.midi_cc[1];
    st.audible = !t.mute && (!any_solo || t.solo);
    for (const Placement& pl : t.arrangement) {
      const SnapClip* c = snap_clip(pl.clip_id);
      if (!c || pl.length <= 0) continue;
      st.placements.push_back(SnapPlacement{pl.start, pl.length, pl.offset, c});
    }
    std::sort(st.placements.begin(), st.placements.end(),
              [](const SnapPlacement& a, const SnapPlacement& b) { return a.start < b.start; });
    s->tracks.push_back(std::move(st));
  }
  return s;
}

}  // namespace twill
