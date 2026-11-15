#include "gfx/Canvas.h"

#include <cmath>

namespace twill {

namespace {
constexpr size_t kMaxDirtyRects = 32;  // beyond this, collapse to one bounding rectangle
}

void Canvas::resize(int width, int height) {
  width_ = width;
  height_ = height;
  pixels_.assign(size_t(width) * height, 0xffffffffu);
  reset_clip();
  dirty_.clear();
  mark_all_dirty();
}

void Canvas::set_clip(const Rect& r) { clip_ = r.intersect(Rect{0, 0, width_, height_}); }

namespace {
// Blends src over dst with alpha a (0-256), red and blue in one multiply, green in another.
// Dividing by 256 instead of 255 is off by at most one step, invisible on screen.
inline uint32_t blend_px(uint32_t dst, uint32_t src, uint32_t a) {
  uint32_t ia = 256 - a;
  uint32_t rb = (((src & 0xff00ffu) * a + (dst & 0xff00ffu) * ia) >> 8) & 0xff00ffu;
  uint32_t g = (((src & 0x00ff00u) * a + (dst & 0x00ff00u) * ia) >> 8) & 0x00ff00u;
  return 0xff000000u | rb | g;
}
// 0-255 alpha to 0-256, so that 255 means fully opaque.
inline uint32_t alpha256(uint32_t a) { return a + (a >> 7); }
}  // namespace

inline void Canvas::blend(uint32_t& dst, Color c, int coverage) {
  uint32_t a = alpha256(uint32_t(c.a()) * uint32_t(coverage) >> 8);
  if (a == 0) return;
  dst = a >= 255 ? (c.argb | 0xff000000u) : blend_px(dst, c.argb, a);
}

void Canvas::fill(const Rect& r, Color c) {
  Rect cr = r.intersect(clip_);
  if (cr.empty()) return;
  mark_dirty(cr);
  uint32_t* p = &pixels_[size_t(cr.y) * width_ + cr.x];
  const uint32_t a = alpha256(c.a());
  if (cr.w == 1) {  // vertical lines: step down the column directly
    if (a >= 256) {
      for (int y = 0; y < cr.h; ++y, p += width_) *p = c.argb;
    } else {
      for (int y = 0; y < cr.h; ++y, p += width_) *p = blend_px(*p, c.argb, a);
    }
    return;
  }
  for (int y = 0; y < cr.h; ++y, p += width_) {
    if (a >= 256) {
      std::fill(p, p + cr.w, c.argb);
    } else {
      for (int x = 0; x < cr.w; ++x) p[x] = blend_px(p[x], c.argb, a);
    }
  }
}

void Canvas::fill_rounded(const Rect& r, float radius, Color c) {
  radius = std::min(radius, std::min(r.w, r.h) * 0.5f);
  if (radius < 0.5f) {
    fill(r, c);
    return;
  }
  Rect cr = r.intersect(clip_);
  if (cr.empty()) return;
  int ir = int(std::ceil(radius));
  // The middle band (between the corner rows) is a plain rectangle: use the fast fill for it, and
  // run the per-pixel corner code only on the top and bottom bands.
  Rect middle{r.x, r.y + ir, r.w, r.h - 2 * ir};
  fill(middle, c);
  mark_dirty(cr);
  for (int y = cr.y; y < cr.bottom(); ++y) {
    if (y >= middle.y && y < middle.bottom()) continue;
    uint32_t* row = &pixels_[size_t(y) * width_];
    // Distance into a corner, measured from the corner circle's centre (pixel centres at +0.5).
    float py = y + 0.5f;
    float dy = 0;
    if (py < r.y + radius) dy = (r.y + radius) - py;
    else if (py > r.bottom() - radius) dy = py - (r.bottom() - radius);
    for (int x = cr.x; x < cr.right(); ++x) {
      int coverage = 255;
      if (dy > 0 && (x < r.x + ir || x >= r.right() - ir)) {
        float px = x + 0.5f;
        float dx = 0;
        if (px < r.x + radius) dx = (r.x + radius) - px;
        else if (px > r.right() - radius) dx = px - (r.right() - radius);
        float d = std::sqrt(dx * dx + dy * dy) - radius;  // <0 inside, >0 outside
        coverage = d <= -0.5f ? 255 : d >= 0.5f ? 0 : int((0.5f - d) * 255.0f);
      }
      if (coverage) blend(row[x], c, coverage);
    }
  }
}

void Canvas::stroke_rect(const Rect& r, int t, Color c) {
  fill(Rect{r.x, r.y, r.w, t}, c);
  fill(Rect{r.x, r.bottom() - t, r.w, t}, c);
  fill(Rect{r.x, r.y + t, t, r.h - 2 * t}, c);
  fill(Rect{r.right() - t, r.y + t, t, r.h - 2 * t}, c);
}

void Canvas::line(float x0, float y0, float x1, float y1, Color c) {
  // Xiaolin Wu's algorithm.
  bool steep = std::fabs(y1 - y0) > std::fabs(x1 - x0);
  if (steep) {
    std::swap(x0, y0);
    std::swap(x1, y1);
  }
  if (x0 > x1) {
    std::swap(x0, x1);
    std::swap(y0, y1);
  }
  float dx = x1 - x0, dy = y1 - y0;
  float gradient = dx == 0 ? 1 : dy / dx;
  auto plot = [&](int x, int y, float cov) {
    int px = steep ? y : x, py = steep ? x : y;
    if (!clip_.contains(px, py)) return;
    blend(pixels_[size_t(py) * width_ + px], c, int(cov * 255.0f));
  };
  float y = y0;
  for (int x = int(std::round(x0)); x <= int(std::round(x1)); ++x) {
    float fy = std::floor(y);
    float frac = y - fy;
    plot(x, int(fy), 1.0f - frac);
    plot(x, int(fy) + 1, frac);
    y += gradient;
  }
  Rect bounds{int(std::floor(std::min(x0, x1))) - 1, int(std::floor(std::min(y0, y1))) - 1,
              int(std::fabs(dx)) + 3, int(std::fabs(dy)) + 3};
  if (steep) bounds = Rect{bounds.y, bounds.x, bounds.h, bounds.w};
  mark_dirty(bounds.intersect(clip_));
}

float Canvas::text(const Font& font, float x, int baseline, const char* s, Color c) {
  float pen = x;
  Rect touched;
  for (const char* p = s; *p; ++p) {
    const GlyphInfo* g = font.glyph(*p);
    int gx = int(std::round(pen)) + g->ox;
    int gy = baseline + g->oy;
    Rect gr = Rect{gx, gy, g->w, g->h}.intersect(clip_);
    if (!gr.empty()) {
      touched = touched.unite(gr);
      for (int y = gr.y; y < gr.bottom(); ++y) {
        const uint8_t* src = font.atlas() + size_t(g->atlas_y + (y - gy)) * font.atlas_width() +
                             g->atlas_x + (gr.x - gx);
        uint32_t* dst = &pixels_[size_t(y) * width_ + gr.x];
        for (int i = 0; i < gr.w; ++i) {
          uint32_t cov = src[i];
          if (!cov) continue;
          uint32_t a = alpha256(c.a() * cov >> 8);
          dst[i] = a >= 255 ? (c.argb | 0xff000000u) : blend_px(dst[i], c.argb, a);
        }
      }
    }
    pen += g->advance;
  }
  mark_dirty(touched);
  return pen;
}

void Canvas::mark_dirty(const Rect& r) {
  if (r.empty()) return;
  for (Rect& d : dirty_) {
    // Absorb rectangles that overlap or touch an existing one.
    if (r.x <= d.right() && d.x <= r.right() && r.y <= d.bottom() && d.y <= r.bottom()) {
      d = d.unite(r);
      return;
    }
  }
  if (dirty_.size() >= kMaxDirtyRects) {
    Rect all = r;
    for (const Rect& d : dirty_) all = all.unite(d);
    dirty_.assign(1, all);
    return;
  }
  dirty_.push_back(r);
}

std::vector<Rect> Canvas::take_dirty() {
  std::vector<Rect> out;
  out.swap(dirty_);
  return out;
}

}  // namespace twill
