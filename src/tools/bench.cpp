// Drawing benchmark: times the canvas primitives and full app frames without a display, so it can
// run over SSH on a device. Usage: twill_bench [width height scale]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "app/App.h"
#include "engine/Engine.h"
#include "gfx/Canvas.h"
#include "engine/PlaitsVoice.h"
#include "platform/null/NullPlatform.h"

namespace {

using Clock = std::chrono::steady_clock;

template <typename F>
void time(const char* what, int reps, F&& f) {
  auto t0 = Clock::now();
  for (int i = 0; i < reps; ++i) f(i);
  double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count() / reps;
  std::printf("%-34s %9.1f us\n", what, us);
}

}  // namespace

int main(int argc, char** argv) {
  int w = argc > 2 ? std::atoi(argv[1]) : 1024;
  int h = argc > 2 ? std::atoi(argv[2]) : 768;
  float s = argc > 3 ? float(std::atof(argv[3])) : 2.0f;
  std::printf("canvas %dx%d, scale %.2f\n", w, h, s);

  twill::Canvas c;
  c.resize(w, h);
  const twill::Rect all{0, 0, w, h};
  const twill::Color solid = twill::Color::rgb(0x336699), translucent = solid.with_alpha(40);
  const twill::Font& font = twill::Font::get(twill::FontFace::Regular, 13 * s);

  time("fill full screen (opaque)", 50, [&](int) { c.fill(all, solid); });
  time("fill full screen (alpha)", 20, [&](int) { c.fill(all, translucent); });
  time("100 rounded rects 60x24", 50, [&](int i) {
    for (int k = 0; k < 100; ++k) c.fill_rounded(twill::Rect{(k * 37 + i) % (w - 60), (k * 53) % (h - 24), 60, 24}, 3 * s, solid);
  });
  time("100 short strings", 50, [&](int) {
    for (int k = 0; k < 100; ++k) c.text(font, 10, 30 + (k * 7) % (h - 40), "Hello, piano roll 123", solid);
  });
  c.take_dirty();

  // Full app frames: the arrangement, then the piano roll (Select + Right switches).
  twill::NullPlatform platform(w, h, s);
  twill::App app(platform);
  app.start();
  twill::InputState in;
  auto frame = [&](uint32_t held) {
    platform.set_held(held);
    platform.advance_ms(16);
    platform.poll_input(in);
    app.frame(in, c);
    c.take_dirty();
  };
  frame(0);
  time("full frame: arrangement", 20, [&](int i) {
    frame(i % 2 ? twill::kDown : 0);  // moving the cursor redraws the view
  });
  frame(twill::kSelect);
  frame(twill::kSelect | twill::kRight);
  frame(0);
  time("full frame: piano roll", 20, [&](int i) { frame(i % 2 ? twill::kRight : 0); });
  app.stop();

  // Each macro-oscillator engine alone: one held voice, 2 s of audio. Percent of one core.
  if (std::getenv("TWILL_ENGINES")) {
    for (int e = 0; e < twill::PlaitsVoice::kNumEngines; ++e) {
      twill::PlaitsVoice voice;
      twill::MacroParams p;
      p.engine = e;
      p.note = 48;
      voice.note_on();
      float out[256], aux[256];
      const int blocks = 2 * 48000 / 256;
      auto t0 = Clock::now();
      for (int b = 0; b < blocks; ++b) voice.render(p, out, aux, 256);
      const double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
      std::printf("  engine %2d %-18s %5.1f%%\n", e, twill::PlaitsVoice::engine_name(e), 100 * us / 2e6);
    }
  }

  // The engine on the demo song: 10 s of audio with reverb and delay sends on, in 256-frame blocks.
  {
    twill::Project song = app.project();
    for (size_t t = 0; t < song.tracks.size(); ++t) {
      song.tracks[t].send_reverb = t % 2 ? 0.3f : 0.0f;
      song.tracks[t].send_delay = t % 3 == 0 ? 0.2f : 0.0f;
      song.tracks[t].send_chorus = t == 4 ? 0.3f : 0.0f;
    }
    song.loop = true;
    // TWILL_SOLO=n plays only track n; TWILL_DRY=1 turns the sends off. For finding what costs what.
    if (const char* solo = std::getenv("TWILL_SOLO")) {
      const size_t keep = size_t(std::atoi(solo));
      for (size_t t = 0; t < song.tracks.size(); ++t)
        if (t != keep) song.tracks[t].arrangement.clear();
    }
    if (std::getenv("TWILL_DRY"))
      for (auto& t : song.tracks) t.send_reverb = t.send_delay = t.send_chorus = 0;
    twill::Engine engine(platform.max_voices());
    engine.submit(twill::build_snapshot(song, platform.max_tracks(), 1));
    engine.play();
    std::vector<float> buf(512);
    const int blocks = 10 * 48000 / 256;
    std::vector<double> times(size_t(blocks), 0.0);
    std::vector<int> voices(size_t(blocks), 0);
    double total = 0;
    for (int b = 0; b < blocks; ++b) {
      auto t0 = Clock::now();
      engine.render(buf.data(), 256);
      times[size_t(b)] = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
      voices[size_t(b)] = engine.active_voices();
      total += times[size_t(b)];
    }
    const double budget = 1e6 * 256 / 48000;
    const int over_half = int(std::count_if(times.begin(), times.end(), [&](double t) { return t > budget / 2; }));
    const int first_slow = int(std::max_element(times.begin(), times.end()) - times.begin());
    std::vector<double> sorted = times;
    std::sort(sorted.begin(), sorted.end());
    std::printf("engine, demo song with effects: %.1f%% of a core on average\n", 100 * total / blocks / budget);
    std::printf("  median %.1f%%, 99th percentile %.1f%%, worst %.1f%% (block %d of %d), %d blocks over 50%%\n",
                100 * sorted[sorted.size() / 2] / budget, 100 * sorted[sorted.size() * 99 / 100] / budget,
                100 * sorted.back() / budget, first_slow, blocks, over_half);
    if (std::getenv("TWILL_DETAIL")) {
      std::vector<int> order(static_cast<size_t>(blocks));
      for (int b = 0; b < blocks; ++b) order[size_t(b)] = b;
      std::sort(order.begin(), order.end(), [&](int a, int b) { return times[size_t(a)] > times[size_t(b)]; });
      for (int k = 0; k < 12; ++k) {
        const int b = order[size_t(k)];
        std::printf("  block %4d: %5.1f%%, %d voices (neighbours %d, %d)\n", b, 100 * times[size_t(b)] / budget,
                    voices[size_t(b)], b > 0 ? voices[size_t(b - 1)] : 0, b + 1 < blocks ? voices[size_t(b + 1)] : 0);
      }
      int maxv = 0;
      for (int v : voices) maxv = std::max(maxv, v);
      std::printf("  most voices at once: %d\n", maxv);
    }
  }
  return 0;
}
