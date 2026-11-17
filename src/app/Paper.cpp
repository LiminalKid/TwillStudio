#include "app/Paper.h"

#include <algorithm>
#include <cctype>

namespace twill {

std::string caps(const std::string& s) {
  std::string out = s;
  for (char& ch : out) ch = char(std::toupper(static_cast<unsigned char>(ch)));
  return out;
}

void draw_brackets(Canvas& c, const Rect& r, Color col, float u) {
  const int off = int(3 * u);
  const int th = std::max(2, int(2 * u));
  const int len = int(std::max(5.0f * u, std::min({8.0f * u, r.w / 3.0f, r.h / 3.0f})));
  const int x0 = r.x - off, y0 = r.y - off, x1 = r.right() + off, y1 = r.bottom() + off;
  c.fill(Rect{x0, y0, len, th}, col);
  c.fill(Rect{x0, y0, th, len}, col);
  c.fill(Rect{x1 - len, y0, len, th}, col);
  c.fill(Rect{x1 - th, y0, th, len}, col);
  c.fill(Rect{x0, y1 - th, len, th}, col);
  c.fill(Rect{x0, y1 - len, th, len}, col);
  c.fill(Rect{x1 - len, y1 - th, len, th}, col);
  c.fill(Rect{x1 - th, y1 - len, th, len}, col);
}

void draw_shadow(Canvas& c, int edge, int y, int h, int width, bool leftwards, int strength) {
  for (int k = 0; k < width; ++k) {
    const float f = 1.0f - float(k) / width;
    const int a = int(strength * f * f);
    if (a <= 0) continue;
    const int x = leftwards ? edge - 1 - k : edge + k;
    c.fill(Rect{x, y, 1, h}, Color::rgb(0).with_alpha(uint8_t(a)));
  }
}

void draw_marker(Canvas& c, int x, int tip_y, int half, Color col) {
  for (int i = 0; i <= half; ++i) c.fill(Rect{x - i, tip_y - i, 2 * i + 1, 1}, col);
}

int keycap_width(const Font& f, const char* key, float u) {
  return std::max(int(16 * u), int(f.measure(key) + 8 * u));
}

int draw_keycap(Canvas& c, const Font& f, int x, int y, int h, const char* key, Color box, Color text,
                bool outline, float u) {
  const int w = keycap_width(f, key, u);
  if (outline) c.stroke_rect(Rect{x, y, w, h}, std::max(1, int(u)), box);
  else c.fill(Rect{x, y, w, h}, box);
  c.text(f, x + (w - f.measure(key)) / 2, f.baseline_in(y, h), key, text);
  return w;
}

}  // namespace twill
