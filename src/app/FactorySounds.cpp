#include "app/FactorySounds.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "engine/PlaitsVoice.h"
#include "engine/Sample.h"

namespace twill {

namespace {

struct Recipe {
  const char* name;
  int engine;
  int note;
  float harmonics, timbre, morph, decay;
  float gate_seconds;  // drums: a short trigger; pitched sounds: held
  float length_seconds;
};

// Kit order: kicks, snares, hats, toms and percussion, then pitched sounds at C3.
const Recipe kRecipes[] = {
    {"Kick", 21, 36, 0.3f, 0.4f, 0.5f, 0.5f, 0.05f, 1.2f},
    {"Kick long", 21, 33, 0.2f, 0.3f, 0.6f, 0.75f, 0.05f, 2.0f},
    {"Snare", 22, 55, 0.5f, 0.5f, 0.5f, 0.5f, 0.05f, 1.0f},
    {"Snare tight", 22, 60, 0.75f, 0.6f, 0.4f, 0.35f, 0.05f, 0.8f},
    {"Hat closed", 23, 72, 0.5f, 0.5f, 0.5f, 0.22f, 0.05f, 0.5f},
    {"Hat open", 23, 72, 0.5f, 0.5f, 0.5f, 0.6f, 0.05f, 1.5f},
    {"Tom low", 21, 45, 0.7f, 0.6f, 0.3f, 0.5f, 0.05f, 1.2f},
    {"Tom high", 21, 52, 0.7f, 0.6f, 0.3f, 0.45f, 0.05f, 1.0f},
    {"Rim", 20, 79, 0.8f, 0.7f, 0.2f, 0.18f, 0.05f, 0.5f},
    {"Noise hit", 17, 72, 0.6f, 0.7f, 0.4f, 0.35f, 0.05f, 0.8f},
    {"Bell", 20, 72, 0.4f, 0.6f, 0.6f, 0.7f, 0.05f, 2.5f},
    {"Particles", 18, 60, 0.5f, 0.5f, 0.5f, 0.45f, 0.05f, 1.5f},
    {"Zap", 7, 84, 0.3f, 0.2f, 0.7f, 0.35f, 0.05f, 0.8f},
    {"Pluck C3", 19, 48, 0.4f, 0.5f, 0.5f, 0.6f, 1.0f, 2.5f},
    {"Keys C3", 10, 48, 0.3f, 0.4f, 0.4f, 0.7f, 1.2f, 2.5f},
    {"Pad C3", 6, 48, 0.4f, 0.3f, 0.6f, 0.8f, 2.0f, 3.0f},
};
constexpr int kNumRecipes = int(sizeof kRecipes / sizeof kRecipes[0]);

std::string path_for(int i) {
  char b[96];
  std::snprintf(b, sizeof b, "%s/%02d %s.wav", kFactoryFolder, i + 1, kRecipes[i].name);
  return b;
}

std::vector<float> render(const Recipe& r) {
  PlaitsVoice::seed_random(0x21 + uint32_t(r.engine));
  auto voice = std::make_unique<PlaitsVoice>();
  MacroParams p;
  p.engine = r.engine;
  p.note = float(r.note);
  p.harmonics = r.harmonics;
  p.timbre = r.timbre;
  p.morph = r.morph;
  p.decay = r.decay;
  const int frames = int(r.length_seconds * PlaitsVoice::kSampleRate);
  const int gate = int(r.gate_seconds * PlaitsVoice::kSampleRate);
  std::vector<float> mono(static_cast<size_t>(frames));
  voice->note_on();
  for (int pos = 0; pos < frames; pos += 48) {
    if (pos >= gate && pos < gate + 48) voice->note_off();
    voice->render(p, &mono[size_t(pos)], nullptr, std::min(48, frames - pos));
  }
  // Trim the silent tail, fade the last few milliseconds, and normalize to -1 dB.
  int end = frames;
  while (end > 1 && std::fabs(mono[size_t(end - 1)]) < 2e-4f) --end;
  const int fade = std::min(end, 240);
  for (int i = 0; i < fade; ++i) mono[size_t(end - fade + i)] *= 1.0f - float(i) / fade;
  float peak = 1e-6f;
  for (int i = 0; i < end; ++i) peak = std::max(peak, std::fabs(mono[size_t(i)]));
  const float gain = 0.891f / peak;
  std::vector<float> stereo(size_t(end) * 2);
  for (int i = 0; i < end; ++i) stereo[size_t(2 * i)] = stereo[size_t(2 * i + 1)] = mono[size_t(i)] * gain;
  return stereo;
}

}  // namespace

const std::vector<std::string>& factory_sound_paths() {
  static const std::vector<std::string> paths = [] {
    std::vector<std::string> v;
    for (int i = 0; i < kNumRecipes; ++i) v.push_back(path_for(i));
    return v;
  }();
  return paths;
}

int make_factory_sounds(Platform& platform) {
  int written = 0;
  std::vector<uint8_t> existing;
  for (int i = 0; i < kNumRecipes; ++i) {
    const std::string path = path_for(i);
    if (platform.read_file(path.c_str(), existing)) continue;
    if (written == 0) platform.make_dir(kFactoryFolder);
    std::vector<float> st = render(kRecipes[i]);
    if (platform.write_file(path.c_str(), encode_wav16(st.data(), int(st.size() / 2)))) ++written;
  }
  PlaitsVoice::seed_random(0x21);
  return written;
}

}  // namespace twill
