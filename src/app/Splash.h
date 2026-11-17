// The launch splash: the logo (tools/make-logo.py) on a faint twill, shown while the app starts.
#pragma once
#include "gfx/Canvas.h"

namespace twill {

// Fills the canvas with the splash, the logo scaled up by whole pixels to suit the screen.
void draw_splash(Canvas& c);

}  // namespace twill
