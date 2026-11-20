// Audio samples (design §6, "Samples"): decoded from WAV or AIFF and converted to stereo float at
// the engine's 48 kHz, so voices never convert while playing.
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
  std::vector<float> data;  // interleaved stereo, frames * 2
  std::vector<Onset> onsets;  // in order; found when decoded (find_onsets)

  float left(int i) const { return data[size_t(2 * i)]; }
  float right(int i) const { return data[size_t(2 * i + 1)]; }
};

// Decodes WAV or AIFF (8/16/24/32-bit, float, mono or stereo). Returns null and sets `error` on
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
