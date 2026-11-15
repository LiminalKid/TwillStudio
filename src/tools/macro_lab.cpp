// Macro-oscillator lab (milestone M1): renders every engine to WAV, and benchmarks every engine at a
// device's audio block size. Needs no display or sound device, so it runs over SSH on a handheld.
//
//   twill_macro_lab render <out-dir>      one WAV per engine
//   twill_macro_lab bench [block-frames]  cost per engine (default 256-frame blocks)
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "engine/PlaitsVoice.h"

namespace {

using Clock = std::chrono::steady_clock;
constexpr int kRate = twill::PlaitsVoice::kSampleRate;

bool write_wav(const std::string& path, const std::vector<float>& mono) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
  uint32_t data_bytes = uint32_t(mono.size() * 2);
  std::fwrite("RIFF", 1, 4, f); u32(36 + data_bytes); std::fwrite("WAVEfmt ", 1, 8, f);
  u32(16); u16(1); u16(1); u32(kRate); u32(kRate * 2); u16(2); u16(16);
  std::fwrite("data", 1, 4, f); u32(data_bytes);
  for (float s : mono) {
    int v = int(std::lround(std::max(-1.0f, std::min(1.0f, s)) * 32767.0f));
    u16(uint16_t(int16_t(v)));
  }
  return std::fclose(f) == 0;
}

// A short phrase per engine: notes every half second over two octaves, with timbre and morph
// sweeping slowly so each recording covers the engine's range.
void play_phrase(twill::PlaitsVoice& v, twill::MacroParams p, float seconds, float* out, float* aux,
                 int block, bool record) {
  static const int kNotes[] = {48, 55, 60, 63, 67, 72, 70, 65, 60, 58, 55, 51};
  const int total = int(seconds * kRate);
  const int note_len = kRate / 2;
  for (int pos = 0; pos < total; pos += block) {
    int n = std::min(block, total - pos);
    float t = float(pos) / total;
    p.timbre = 0.5f + 0.45f * std::sin(t * 6.2832f);
    p.morph = 0.5f - 0.45f * std::cos(t * 6.2832f * 0.5f);
    if (pos % note_len < block) {
      p.note = float(kNotes[(pos / note_len) % 12]);
      v.note_on();
    } else if (pos % note_len >= note_len * 3 / 4 && pos % note_len < note_len * 3 / 4 + block) {
      v.note_off();  // notes are held for three quarters of their slot
    }
    v.render(p, record ? out + pos : out, record ? aux + pos : aux, n);
  }
}

int render(const char* dir) {
  for (int e = 0; e < twill::PlaitsVoice::kNumEngines; ++e) {
    auto voice = std::make_unique<twill::PlaitsVoice>();
    twill::MacroParams p;
    p.engine = e;
    p.decay = 0.55f;
    const float seconds = 6.0f;
    std::vector<float> out(size_t(seconds * kRate)), aux(out.size());
    play_phrase(*voice, p, seconds, out.data(), aux.data(), 256, true);
    char name[256];
    std::string slug = twill::PlaitsVoice::engine_name(e);
    for (char& c : slug) c = std::isalnum(static_cast<unsigned char>(c)) ? char(std::tolower(c)) : '-';
    std::snprintf(name, sizeof name, "%s/%02d-%s.wav", dir, e, slug.c_str());
    float peak = 0;
    for (float s : out) peak = std::max(peak, std::fabs(s));
    std::printf("%-60s peak %.2f\n", name, peak);
    if (!write_wav(name, out)) {
      std::fprintf(stderr, "could not write %s\n", name);
      return 1;
    }
  }
  return 0;
}

int bench(int block) {
  const double budget_us = 1e6 * block / kRate;
  std::printf("block %d frames = %.0f us of audio. Cost per voice, as %% of one core:\n", block, budget_us);
  std::printf("%-3s %-18s %8s %8s %10s\n", "#", "engine", "avg %", "max %", "voices/core");
  std::vector<float> out(block), aux(block);
  double worst_avg = 0;
  for (int e = 0; e < twill::PlaitsVoice::kNumEngines; ++e) {
    auto voice = std::make_unique<twill::PlaitsVoice>();
    twill::MacroParams p;
    p.engine = e;
    // Warm up (first blocks after an engine change do extra setup), then measure 4 s of audio
    // with a new note every quarter second.
    play_phrase(*voice, p, 0.5f, out.data(), aux.data(), block, false);
    const int blocks = 4 * kRate / block;
    double total = 0, worst = 0;
    for (int b = 0; b < blocks; ++b) {
      const int slot = kRate / 4 / block + 1;  // a new note every quarter second
      if (b % slot == 0) {
        p.note = float(48 + (b * 7) % 24);
        voice->note_on();
      } else if (b % slot == slot * 3 / 4) {
        voice->note_off();
      }
      p.timbre = 0.5f + 0.45f * std::sin(b * 0.01f);
      auto t0 = Clock::now();
      voice->render(p, out.data(), aux.data(), block);
      double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
      total += us;
      worst = std::max(worst, us);
    }
    double avg_pct = 100.0 * total / blocks / budget_us, max_pct = 100.0 * worst / budget_us;
    worst_avg = std::max(worst_avg, avg_pct);
    std::printf("%-3d %-18s %8.1f %8.1f %10d\n", e, twill::PlaitsVoice::engine_name(e), avg_pct, max_pct,
                int(100.0 / avg_pct));
  }
  std::printf("most expensive engine: %.1f%% of a core per voice on average\n", worst_avg);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 3 && std::strcmp(argv[1], "render") == 0) return render(argv[2]);
  if (argc >= 2 && std::strcmp(argv[1], "bench") == 0) return bench(argc >= 3 ? std::atoi(argv[2]) : 256);
  std::fprintf(stderr, "usage: %s render <out-dir> | bench [block-frames]\n", argv[0]);
  return 2;
}
