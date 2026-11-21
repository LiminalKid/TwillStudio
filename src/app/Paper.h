// Drawing pieces shared by the views in the paper look: capitals, the viewfinder cursor, keycaps,
// the playhead's marker and soft shadows.
#pragma once
#include <string>

#include "gfx/Canvas.h"

namespace twill {

// Letter spacing for small capitals, in points (times the UI scale).
constexpr float kCapsSpacing = 0.5f;

std::string caps(const std::string& s);

// `s` as it fits in `width` pixels: whole if it does, else cut short with "..." (a value in a
// settings row, so it doesn't run into the row's label).
std::string fit_text(const Font& f, const std::string& s, float width, float spacing = 0);

// The cursor: four corner marks just outside `r`, like a viewfinder. `u` is the UI scale.
void draw_brackets(Canvas& c, const Rect& r, Color col, float u);

// A soft shadow `width` pixels wide cast by an edge at x = `edge`, falling to the left of it
// (leftwards) or to the right, over rows y to y + h. Darkest at the edge, at `strength` alpha.
void draw_shadow(Canvas& c, int edge, int y, int h, int width, bool leftwards, int strength);

// A downward triangle with its tip at (x, tip_y), `half` pixels wide each side: the playhead's
// marker in a ruler.
void draw_marker(Canvas& c, int x, int tip_y, int half, Color col);

// A button name in a box (a keycap) with its top at y, `h` high; returns its width.
int draw_keycap(Canvas& c, const Font& f, int x, int y, int h, const char* key, Color box, Color text,
                bool outline, float u);
int keycap_width(const Font& f, const char* key, float u);

}  // namespace twill
