#include "engine/Sample.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO  // we read files through the platform layer
#include "dr_wav.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG   // FLAC files only, not FLAC in Ogg
#include "dr_flac.h"

namespace twill {

namespace {

constexpr int kEngineRate = 48000;

// 4-point Hermite interpolation.
inline float hermite(float xm1, float x0, float x1, float x2, float t) {
  const float c = (x1 - xm1) * 0.5f;
  const float v = x0 - x1;
  const float w = c + v;
  const float a = w + v + (x2 - x0) * 0.5f;
  const float b = w + a;
  return ((a * t - b) * t + c) * t + x0;
}

}  // namespace

std::shared_ptr<const SampleData> decode_sample(const uint8_t* bytes, size_t size, const std::string& name,
                                                std::string& error) {
  // The file's audio as interleaved float, whichever reader it takes.
  int channels = 0, rate = 0;
  uint64_t frames = 0, read = 0;
  std::vector<float> raw;
  int64_t marker_a = -1, marker_b = -1;  // the file's own loop, in its frames
  auto too_long = [&] { return frames > uint64_t(48000) * 60 * 30; };
  if (size >= 4 && std::memcmp(bytes, "fLaC", 4) == 0) {
    drflac* flac = drflac_open_memory(bytes, size, nullptr);
    if (!flac) {
      error = "a FLAC file this app can't read";
      return nullptr;
    }
    channels = int(flac->channels);
    rate = int(flac->sampleRate);
    frames = flac->totalPCMFrameCount;
    if (channels < 1 || frames == 0 || rate == 0 || too_long()) {
      drflac_close(flac);
      error = frames == 0 ? "the file has no audio" : "the file is too long or malformed";
      return nullptr;
    }
    raw.resize(size_t(frames) * size_t(channels));
    read = drflac_read_pcm_frames_f32(flac, frames, raw.data());
    drflac_close(flac);
  } else {
    drwav wav;
    if (!drwav_init_memory_with_metadata(&wav, bytes, size, 0, nullptr)) {
      error = "not a WAV, AIFF or FLAC file this app can read";
      return nullptr;
    }
    channels = int(wav.channels);
    rate = int(wav.sampleRate);
    frames = wav.totalPCMFrameCount;
    if (channels < 1 || frames == 0 || rate == 0 || too_long()) {
      drwav_uninit(&wav);
      error = frames == 0 ? "the file has no audio" : "the file is too long or malformed";
      return nullptr;
    }
    raw.resize(size_t(frames) * size_t(channels));
    read = drwav_read_pcm_frames_f32(&wav, frames, raw.data());
    // Loop markers (a sampler chunk's first loop), for multisample zones that don't set their own.
    for (drwav_uint32 i = 0; i < wav.metadataCount; ++i)
      if (wav.pMetadata[i].type == drwav_metadata_type_smpl && wav.pMetadata[i].data.smpl.sampleLoopCount > 0) {
        const drwav_smpl_loop& l = wav.pMetadata[i].data.smpl.pLoops[0];
        if (l.lastSampleOffset > l.firstSampleOffset) marker_a = l.firstSampleOffset, marker_b = int64_t(l.lastSampleOffset) + 1;
        break;
      }
    drwav_uninit(&wav);
  }
  if (read == 0) {
    error = "the audio couldn't be decoded";
    return nullptr;
  }

  // Mono stays mono; more than two channels keep the first two.
  const int keep = std::min(channels, 2);
  std::vector<float> in(size_t(read) * size_t(keep));
  for (uint64_t i = 0; i < read; ++i)
    for (int ch = 0; ch < keep; ++ch) in[size_t(i) * size_t(keep) + size_t(ch)] = raw[size_t(i) * size_t(channels) + size_t(ch)];

  auto s = std::make_shared<SampleData>();
  s->name = name;
  s->source_rate = rate;
  s->source_channels = keep;
  if (rate == kEngineRate) {
    s->set_from_float(in, keep);
  } else {
    // Resample to 48 kHz.
    const double ratio = double(rate) / kEngineRate;
    const int out_frames = int(double(read) / ratio);
    std::vector<float> out(size_t(out_frames) * size_t(keep));
    const int last = int(read) - 1;
    auto at = [&](int i, int ch) { return in[size_t(std::max(0, std::min(last, i))) * size_t(keep) + size_t(ch)]; };
    for (int i = 0; i < out_frames; ++i) {
      const double pos = i * ratio;
      const int i0 = int(pos);
      const float t = float(pos - i0);
      for (int ch = 0; ch < keep; ++ch)
        out[size_t(i) * size_t(keep) + size_t(ch)] = hermite(at(i0 - 1, ch), at(i0, ch), at(i0 + 1, ch), at(i0 + 2, ch), t);
    }
    s->set_from_float(out, keep);
  }
  if (marker_a >= 0 && marker_b <= int64_t(read)) {
    const double scale = double(kEngineRate) / rate;
    s->loop_start = std::min(s->frames - 1, int(std::lround(marker_a * scale)));
    s->loop_end = std::max(s->loop_start + 1, std::min(s->frames, int(std::lround(marker_b * scale))));
  }
  s->onsets = find_onsets(*s);
  return s;
}

void SampleData::set_from_float(const std::vector<float>& in, int ch) {
  channels = ch < 2 ? 1 : 2;
  frames = int(in.size() / size_t(channels));
  pcm.resize(size_t(frames) * size_t(channels));
  for (size_t i = 0; i < pcm.size(); ++i)
    pcm[i] = int16_t(std::lrint(std::max(-1.0f, std::min(1.0f, in[i])) * 32767.0f));
}

// A spectral transient detector at a fixed high sensitivity (0.8): it finds plenty, each with a strength, and transient_slices() keeps the strongest that fit.
std::vector<Onset> find_onsets(const SampleData& s) {
  constexpr float kSensitivity = 0.8f;
  constexpr int kMinGap = 48000 * 30 / 1000;  // hits closer than 30 ms count as one
  const int hop = 240;                         // 5 ms
  const int frames = s.frames / hop;
  std::vector<Onset> out;
  if (frames < 3) return out;
  // Each frame's energy, and its treble's (the change from sample to sample, which a hit's attack is
  // full of).
  std::vector<float> energy(size_t(frames), 0.0f), treble(size_t(frames), 0.0f);
  float peak_energy = 0;
  for (int f = 0; f < frames; ++f) {
    double e = 0, t = 0;
    for (int ch = 0; ch < 2; ++ch)
      for (int i = f * hop; i < (f + 1) * hop; ++i) {
        const double v = s.at(i, ch);
        const double d = v - (i > 0 ? double(s.at(i - 1, ch)) : 0.0);
        e += v * v;
        t += d * d;
      }
    energy[size_t(f)] = float(e / (2 * hop));
    treble[size_t(f)] = float(t / (2 * hop));
    peak_energy = std::max(peak_energy, energy[size_t(f)]);
  }
  if (peak_energy <= 0) return out;
  // How sharply each rises from the two frames before it, in dB (the treble counts double).
  const float floor = 1e-10f;
  std::vector<float> onset(size_t(frames), 0.0f);
  for (int f = 1; f < frames; ++f) {
    auto rise = [&](const std::vector<float>& v) {
      float before = v[size_t(f) - 1];
      if (f >= 2) before = std::max(before, v[size_t(f) - 2]);
      return std::max(0.0f, 10.0f * std::log10((v[size_t(f)] + floor) / (before + floor)));
    };
    onset[size_t(f)] = (2.0f * rise(treble) + rise(energy)) / 3.0f;
  }
  // A hit: a peak in that rise, above a threshold that follows the music round it, and loud enough
  // to matter.
  const float delta = 18.0f + (1.5f - 18.0f) * kSensitivity;  // dB above the local median
  const float gate = peak_energy * std::pow(10.0f, (-12.0f + (-60.0f + 12.0f) * kSensitivity) / 10.0f);
  constexpr int kAround = 8;
  int last_hit = 0;
  std::vector<float> local;
  for (int f = 1; f < frames - 1; ++f) {
    const float value = onset[size_t(f)];
    if (value < onset[size_t(f) - 1] || value < onset[size_t(f) + 1]) continue;
    if (energy[size_t(f)] < gate && energy[size_t(f) + 1] < gate) continue;
    local.clear();
    for (int g = std::max(0, f - kAround); g <= std::min(frames - 1, f + kAround); ++g) local.push_back(onset[size_t(g)]);
    std::nth_element(local.begin(), local.begin() + long(local.size() / 2), local.end());
    const float median = local[local.size() / 2];
    if (value < median + delta) continue;
    // The attack: from a frame before, the first sample at a tenth of the peak round it, then back
    // (at most 2 ms) to a zero crossing so the slice doesn't click.
    const int frame_start = f * hop;
    const int from = std::max(0, frame_start - hop), to = std::min(s.frames, frame_start + hop);
    auto level = [&](int i) { return std::max(std::fabs(s.left(i)), std::fabs(s.right(i))); };
    float frame_peak = 0;
    for (int i = from; i < to; ++i) frame_peak = std::max(frame_peak, level(i));
    int attack = frame_start;
    for (int i = from; i < to; ++i)
      if (level(i) >= frame_peak * 0.1f) {
        attack = i;
        break;
      }
    const int limit = std::max(0, attack - 96);
    for (int i = attack; i > limit; --i) {
      const float x0 = s.left(i - 1), x1 = s.left(i);
      if ((x0 <= 0 && x1 >= 0) || (x0 >= 0 && x1 <= 0)) {
        attack = std::fabs(x0) < std::fabs(x1) ? i - 1 : i;
        break;
      }
    }
    // Strength: how far the rise stands above its surroundings, favouring louder hits.
    const float strength = value - median + 0.25f * 10.0f * std::log10((energy[size_t(f)] + floor) / peak_energy);
    if (!out.empty() && attack - last_hit < kMinGap) continue;
    out.push_back(Onset{attack, strength});
    last_hit = attack;
  }
  return out;
}

std::vector<int> transient_slices(const SampleData& s, float start, float end, int max_slices) {
  const int a = std::max(0, std::min(s.frames - 1, int(start * s.frames)));
  const int b = std::max(a + 1, std::min(s.frames, int(end * s.frames)));
  constexpr int kMinSlice = 48000 * 20 / 1000;  // onsets right at the region's start are its start
  std::vector<Onset> inside;
  for (const Onset& o : s.onsets)
    if (o.frame >= a + kMinSlice && o.frame < b - kMinSlice) inside.push_back(o);
  const size_t keep = size_t(std::max(0, max_slices - 1));
  if (inside.size() > keep) {
    std::stable_sort(inside.begin(), inside.end(), [](const Onset& x, const Onset& y) { return x.strength > y.strength; });
    inside.resize(keep);
    std::sort(inside.begin(), inside.end(), [](const Onset& x, const Onset& y) { return x.frame < y.frame; });
  }
  std::vector<int> points = {a};
  for (const Onset& o : inside) points.push_back(o.frame);
  points.push_back(b);
  return points;
}

std::vector<uint8_t> encode_wav16(const float* stereo, int frames, int sample_rate) {
  std::vector<uint8_t> out;
  auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i))); };
  auto u16 = [&](uint16_t v) { out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8)); };
  const uint32_t bytes = uint32_t(frames) * 4;
  out.insert(out.end(), {'R', 'I', 'F', 'F'});
  u32(36 + bytes);
  out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
  u32(16); u16(1); u16(2); u32(uint32_t(sample_rate)); u32(uint32_t(sample_rate) * 4); u16(4); u16(16);
  out.insert(out.end(), {'d', 'a', 't', 'a'});
  u32(bytes);
  for (int i = 0; i < frames * 2; ++i) {
    float v = std::max(-1.0f, std::min(1.0f, stereo[i]));
    u16(uint16_t(int16_t(std::lround(v * 32767.0f))));
  }
  return out;
}

std::vector<uint8_t> encode_wav_float(const float* stereo, int frames, int sample_rate) {
  std::vector<uint8_t> out;
  auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i))); };
  auto u16 = [&](uint16_t v) { out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8)); };
  const uint32_t bytes = uint32_t(frames) * 8;
  out.insert(out.end(), {'R', 'I', 'F', 'F'});
  u32(4 + 26 + 12 + 8 + bytes);
  // WAVE_FORMAT_IEEE_FLOAT: an 18-byte fmt chunk and a fact chunk, as the format asks.
  out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
  u32(18); u16(3); u16(2); u32(uint32_t(sample_rate)); u32(uint32_t(sample_rate) * 8); u16(8); u16(32); u16(0);
  out.insert(out.end(), {'f', 'a', 'c', 't'});
  u32(4); u32(uint32_t(frames));
  out.insert(out.end(), {'d', 'a', 't', 'a'});
  u32(bytes);
  for (int i = 0; i < frames * 2; ++i) {
    uint32_t bits;
    std::memcpy(&bits, &stereo[i], 4);
    u32(bits);
  }
  return out;
}

}  // namespace twill
