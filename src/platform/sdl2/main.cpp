// Entry point for SDL2 builds.
#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "app/App.h"
#include "gfx/Canvas.h"
#include "platform/sdl2/Sdl2Platform.h"

namespace {
// Writes the canvas as a binary PPM (for TWILL_SCREENSHOT test captures).
bool write_ppm(const twill::Canvas& c, const char* path) {
  FILE* f = std::fopen(path, "wb");
  if (!f) return false;
  std::fprintf(f, "P6 %d %d 255\n", c.width(), c.height());
  for (int i = 0; i < c.width() * c.height(); ++i) {
    uint32_t p = c.pixels()[i];
    unsigned char rgb[3] = {uint8_t(p >> 16), uint8_t(p >> 8), uint8_t(p)};
    std::fwrite(rgb, 1, 3, f);
  }
  return std::fclose(f) == 0;
}
}  // namespace

int main(int, char**) {
  twill::Sdl2Platform platform;
  if (!platform.init()) return 1;
  // Test hook: TWILL_SCREENSHOT=<file.ppm> saves a frame after about a second, then quits.
  const char* screenshot = std::getenv("TWILL_SCREENSHOT");
  int frames = 0;

  twill::App app(platform);
  app.start();
  // Test hook: TWILL_VIEW=roll starts in the piano roll.
  if (const char* v = std::getenv("TWILL_VIEW"))
    if (std::string(v) == "roll") app.show(twill::App::View::PianoRoll);

  twill::Canvas canvas;
  twill::InputState input;
  for (;;) {
    platform.poll_input(input);
    twill::Size size = platform.display_size();
    if (size.w != canvas.width() || size.h != canvas.height()) canvas.resize(size.w, size.h);

    uint64_t t0 = platform.micros();
    if (!app.frame(input, canvas)) break;
    if (screenshot && ++frames == 60) {
      platform.log(write_ppm(canvas, screenshot) ? "saved %s" : "could not write %s", screenshot);
      break;
    }
    std::vector<twill::Rect> dirty = canvas.take_dirty();
    // Nothing changed: keep the last frame on screen rather than presenting it again.
    if (!dirty.empty()) platform.present(canvas.pixels(), canvas.stride(), dirty);
    // Vsync normally paces the loop; without it (or when idle), cap at about 60 frames a second.
    uint64_t spent = platform.micros() - t0;
    if (spent < 15000) SDL_Delay(Uint32((16667 - spent) / 1000));
  }
  app.stop();
  return 0;
}
