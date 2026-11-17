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
  // at E-F and B-C, where there is no black key). Flat, like a drawing of a keyboard.
  c.fill(col, t.key_white);
  const int bottom_note = k.top_note - k.rows + 1;
  for (int note = bottom_note - 1; note <= k.top_note; ++note) {
    if (is_black_key(note)) continue;
    int bottom = is_black_key(note - 1) ? row_top(note - 1) + k.row_h / 2 : row_top(note) + k.row_h;
    if (bottom <= col.y || bottom > col.bottom()) continue;
    c.fill(Rect{col.x, bottom - px, col.w, px}, t.key_white_edge);
  }

  // Black keys: solid ink, 62% of the width, on top of the white keys.
  const int bw = col.w * 62 / 100;
  for (int note = bottom_note; note <= k.top_note; ++note) {
    if (!is_black_key(note)) continue;
    c.fill(Rect{col.x, row_top(note) + px / 2, bw, k.row_h - px}, t.key_black);
  }

  // Octave labels on the C keys, at the front end of the key.
  const Font& label = Font::get(FontFace::CondensedSemiBold, 12 * k.scale);
  for (int note = bottom_note; note <= k.top_note; ++note) {
    if (((note % 12) + 12) % 12 != 0) continue;
    char name[8];
    std::snprintf(name, sizeof name, "C%d", note / 12 - 1);
    int y = row_top(note);
    int baseline = label.baseline_in(y, k.row_h);
    c.text(label, col.right() - 5 * k.scale - label.measure(name), baseline, name, t.text_secondary);
  }
}

}  // namespace twill
