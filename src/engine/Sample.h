// Audio samples (design §6, "Samples"): decoded from WAV or AIFF, resampled to the engine's 48 kHz
// so voices never resample twice, and kept as 16-bit at the file's own channel count (mono stays
// mono): a quarter of the memory of stereo float for a mono sample, half for stereo.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace twill {

// A place where a sound starts (a drum hit, a note), for slicing at transients.
struct Onset {
  int frame = 0;        // just before the attack, at a zero crossing
  float strength = 0;   // how sharp and loud: the strongest are kept when there are too many
};

struct SampleData {
  std::string name;         // file name, for display
  int frames = 0;           // at 48 kHz
  int source_rate = 0;      // the file's own rate
  int source_channels = 0;  // 1 or 2 (more are mixed to two)
  int channels = 2;         // as kept: 1 or 2
  int loop_start = -1, loop_end = -1;  // the file's own loop (a WAV's smpl chunk), in frames at 48 kHz; -1: none
  std::vector<int16_t> pcm; // interleaved, frames * channels
  std::vector<Onset> onsets;  // in order; found when decoded (find_onsets)

  // Frame i of channel ch (0 left, 1 right; a mono sample's one channel is both), -1..1. No range
  // check: callers clamp.
  float at(int i, int ch) const {
    return pcm[size_t(i * channels + (ch < channels ? ch : 0))] * (1.0f / 32767.0f);
  }
  float left(int i) const { return at(i, 0); }
  float right(int i) const { return at(i, 1); }
  size_t bytes() const { return pcm.size() * sizeof(int16_t); }
  // Fills it from interleaved float at 48 kHz (`channels` of them), clipping at full scale.
  void set_from_float(const std::vector<float>& interleaved, int channels);
};

// Decodes WAV, AIFF or FLAC (8/16/24/32-bit, float, mono or stereo). Returns null and sets `error` on
// failure.
std::shared_ptr<const SampleData> decode_sample(const uint8_t* bytes, size_t size, const std::string& name,
                                                std::string& error);

// Finds the sample's onsets (drum hits, plucked notes): where the level, and above all the treble,
// jumps well above the music round it; at least 30 ms apart, each just before its attack at a zero
// crossing.
std::vector<Onset> find_onsets(const SampleData& s);

// Where each slice starts when slicing at transients, in frames, plus where the last one ends:
// the region's start, then the strongest onsets inside it (up to max_slices - 1 of them), in order.
std::vector<int> transient_slices(const SampleData& s, float start, float end, int max_slices);

// Encodes interleaved stereo float at 48 kHz as a 16-bit WAV file.
std::vector<uint8_t> encode_wav16(const float* stereo, int frames, int sample_rate = 48000);
// The same as 32-bit float, which keeps peaks over full scale (stems, with no limiter before them).
std::vector<uint8_t> encode_wav_float(const float* stereo, int frames, int sample_rate = 48000);

}  // namespace twill
