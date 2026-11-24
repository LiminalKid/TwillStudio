// A software canvas: 32-bit pixels at the device's full resolution, a few antialiased primitives,
// clipping, and a list of the regions changed since the last present.
#pragma once
#include <cstdint>
#include <vector>

#include "gfx/Color.h"
#include "gfx/Font.h"
#include "gfx/Geometry.h"

namespace twill {

class Canvas {
 public:
  void resize(int width, int height);

  int width() const { return width_; }
  int height() const { return height_; }
  const uint32_t* pixels() const { return pixels_.data(); }
  int stride() const { return width_; }  // in pixels

  // Clipping: draws only inside the current clip rectangle.
  void set_clip(const Rect& r);
  void reset_clip() { clip_ = Rect{0, 0, width_, height_}; }
  const Rect& clip() const { return clip_; }

  void fill(const Rect& r, Color c);                       // blends if c has alpha < 255
  void fill_rounded(const Rect& r, float radius, Color c);  // antialiased corners
  void stroke_rect(const Rect& r, int thickness, Color c);
  void hline(int x0, int x1, int y, Color c) { fill(Rect{x0, y, x1 - x0, 1}, c); }
  void vline(int x, int y0, int y1, Color c) { fill(Rect{x, y0, 1, y1 - y0}, c); }
  void line(float x0, float y0, float x1, float y1, Color c);  // antialiased, 1 px wide
  // Copies a `w` x `h` image of 32-bit RGB pixels (`stride` pixels a row; its top byte ignored)
  // to (x, y), opaque, inside the clip.
  void blit(const uint32_t* src, int w, int h, int stride, int x, int y);

  // Draws `text` with its baseline at y, with `spacing` extra pixels between letters (small
  // capitals read better spaced out). Returns the x after the last glyph.
  float text(const Font& font, float x, int y, const char* text, Color c, float spacing = 0);

  // Regions changed since the last call to take_dirty().
  void mark_dirty(const Rect& r);
  void mark_all_dirty() { mark_dirty(Rect{0, 0, width_, height_}); }
  std::vector<Rect> take_dirty();

 private:
  inline void blend(uint32_t& dst, Color c, int coverage);  // coverage 0-255

  int width_ = 0, height_ = 0;
  std::vector<uint32_t> pixels_;
  Rect clip_;
  std::vector<Rect> dirty_;
};

}  // namespace twill
