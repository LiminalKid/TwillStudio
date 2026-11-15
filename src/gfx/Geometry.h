#pragma once
#include <algorithm>

namespace twill {

struct Size {
  int w = 0, h = 0;
};

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;

  int right() const { return x + w; }
  int bottom() const { return y + h; }
  bool empty() const { return w <= 0 || h <= 0; }
  bool contains(int px, int py) const { return px >= x && py >= y && px < right() && py < bottom(); }

  Rect intersect(const Rect& o) const {
    int x0 = std::max(x, o.x), y0 = std::max(y, o.y);
    int x1 = std::min(right(), o.right()), y1 = std::min(bottom(), o.bottom());
    return x1 > x0 && y1 > y0 ? Rect{x0, y0, x1 - x0, y1 - y0} : Rect{};
  }
  Rect unite(const Rect& o) const {
    if (empty()) return o;
    if (o.empty()) return *this;
    int x0 = std::min(x, o.x), y0 = std::min(y, o.y);
    return Rect{x0, y0, std::max(right(), o.right()) - x0, std::max(bottom(), o.bottom()) - y0};
  }
  Rect inset(int d) const { return Rect{x + d, y + d, w - 2 * d, h - 2 * d}; }
};

}  // namespace twill
