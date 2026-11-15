// The keyboard down the left of a piano roll, drawn like a real one turned on its side: white keys
// meet halfway across the black key between them, and the black keys are shorter, sit on top and
// look raised.
#pragma once
#include "app/Theme.h"
#include "gfx/Canvas.h"

namespace twill {

bool is_black_key(int note);

struct PianoKeysLayout {
  Rect column;     // the whole keyboard column
  int top_note;    // note of the top row
  int rows;        // number of rows, one semitone each, going down
  int row_h;       // pixels per row
  float scale;     // UI scale, for line widths and bevels
};

void draw_piano_keys(Canvas& c, const PianoKeysLayout& k, const Theme& t);

}  // namespace twill
