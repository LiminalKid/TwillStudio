#include "app/PianoKeys.h"

#include <algorithm>
#include <cstdio>

namespace twill {

bool is_black_key(int note) {
  int n = ((note % 12) + 12) % 12;
  return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

void draw_piano_keys(Canvas& c, const PianoKeysLayout& k, const Theme& t) {
  const Rect& col = k.column;
  const int px = std::max(1, int(k.scale + 0.5f));  // one "point" in pixels
  auto row_top = [&](int note) { return col.y + (k.top_note - note) * k.row_h; };

  // White keys. Each spans its own row plus half of any black key next to it, so the line between
  // two white keys runs through the middle of the black key between them (or along the row edge
  // at E-F and B-C, where there is no black key).
  c.fill(col, t.key_white);
  const int bottom_note = k.top_note - k.rows + 1;
  for (int note = bottom_note - 1; note <= k.top_note; ++note) {
    if (is_black_key(note)) continue;
    int bottom = is_black_key(note - 1) ? row_top(note - 1) + k.row_h / 2 : row_top(note) + k.row_h;
    if (bottom <= col.y || bottom > col.bottom()) continue;
    // A soft line, then the edge: reads as the bottom face of a raised key.
    c.hline(col.x, col.right(), bottom - 2 * px, t.key_white_shade);
    c.fill(Rect{col.x, bottom - px, col.w, px}, t.key_white_edge);
  }

  // Black keys: shorter than the white keys, on top of them, with a shadow on the keys below.
  const int bw = col.w * 6 / 10;
  const int bevel = 3 * px;  // the lighter front end, where the key slopes down
  for (int note = bottom_note; note <= k.top_note; ++note) {
    if (!is_black_key(note)) continue;
    Rect key{col.x, row_top(note) + px, bw, k.row_h - 2 * px};
    c.fill(Rect{key.x, key.y + 2 * px, key.w + 2 * px, key.h}, t.key_shadow);
    c.fill(key, t.key_black);
    c.fill(Rect{key.right() - bevel, key.y, bevel, key.h}, t.key_black_front);
    c.vline(key.right() - bevel, key.y, key.bottom(), t.key_black_edge);
    c.fill(Rect{key.x, key.y, key.w, px}, t.key_black_highlight);
    c.fill(Rect{key.x, key.bottom() - px, key.w, px}, t.key_black_edge);
  }

  // Octave labels on the C keys, at the front end of the key.
  const Font& label = Font::get(FontFace::Regular, 11 * k.scale);
  for (int note = bottom_note; note <= k.top_note; ++note) {
    if (((note % 12) + 12) % 12 != 0) continue;
    char name[8];
    std::snprintf(name, sizeof name, "C%d", note / 12 - 1);
    int y = row_top(note);
    int baseline = y + (k.row_h + label.ascent() - label.descent()) / 2;
    c.text(label, col.right() - 6 * k.scale - label.measure(name), baseline, name, t.text_secondary);
  }
  c.vline(col.right() - 1, col.y, col.bottom(), t.divider);
}

}  // namespace twill
