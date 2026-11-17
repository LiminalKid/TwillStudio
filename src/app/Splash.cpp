#include "app/Splash.h"

#include <algorithm>

#include "app/Theme.h"

namespace twill {

extern const int kLogoWidth, kLogoHeight;
extern const uint8_t kLogoPixels[];

void draw_splash(Canvas& c) {
  // The logo's palette is the app's: graphite, paper, and the orange and green track colours.
  const Theme dark = Theme::dark_theme();
  const Color palette[5] = {Color{0}, dark.background, Theme::light().background, dark.track(1), dark.track(2)};
  const Color texture = Color::rgb(0x202023);
  const int w = c.width(), h = c.height();
  const int scale = std::max(1, std::min(w * 6 / 10 / kLogoWidth, h * 3 / 4 / kLogoHeight));
  // The cloth: a 2/2 twill, faintly, in cells the size of a logo pixel.
  const int cell = std::max(2, scale);
  c.reset_clip();
  for (int y = 0; y < h; ++y)
    for (int x0 = 0; x0 < w; x0 += cell) {
      const bool rib = (((x0 / cell) - (y / cell)) % 4 + 4) % 4 < 2;
      c.fill(Rect{x0, y, std::min(cell, w - x0), 1}, rib ? texture : dark.background);
    }
  const int ox = (w - kLogoWidth * scale) / 2, oy = (h - kLogoHeight * scale) / 2;
  for (int y = 0; y < kLogoHeight; ++y)
    for (int x = 0; x < kLogoWidth; ++x) {
      const uint8_t i = kLogoPixels[y * kLogoWidth + x];
      if (i) c.fill(Rect{ox + x * scale, oy + y * scale, scale, scale}, palette[i]);
    }
}

}  // namespace twill
