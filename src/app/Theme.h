// Colour tokens (design §7: light, paper-like, no glow). The defaults are the light theme; dark()
// is the same design in graphite, chosen in the app's settings.
#pragma once
#include "gfx/Color.h"

namespace twill {

struct Theme {
  Color background = Color::rgb(0xF6F5F2);
  Color panel = Color::rgb(0xFFFFFF);
  Color header = Color::rgb(0xFBFAF8);
  Color divider = Color::rgb(0xDEDCD7);
  Color text = Color::rgb(0x23221F);
  Color text_secondary = Color::rgb(0x77746D);

  Color row_white = Color::rgb(0xFFFFFF);
  Color row_black = Color::rgb(0xF1F0EC);
  Color key_white = Color::rgb(0xFFFFFF);
  Color key_white_shade = Color::rgb(0xEDECE8);  // just above a white key's bottom edge
  Color key_white_edge = Color::rgb(0xC8C6BF);
  Color key_black = Color::rgb(0x2E2D2A);
  Color key_black_front = Color::rgb(0x4A4844);  // the sloped front end
  Color key_black_highlight = Color::rgb(0x66645E);
  Color key_black_edge = Color::rgb(0x161614);
  Color key_shadow = Color::rgb(0x000000).with_alpha(38);
  // The roll's left column when it lists names instead of keys (kit sounds, notes of a scale).
  Color name_column = Color::rgb(0xFFFFFF);
  Color name_column_line = Color::rgb(0xEDECE8);
  Color grid_step = Color::rgb(0xECEBE7);
  Color grid_beat = Color::rgb(0xD9D7D1);
  Color grid_bar = Color::rgb(0xB9B6AE);

  Color note = Color::rgb(0x2F80ED);
  Color note_edge = Color::rgb(0x1F63C4);
  Color cursor = Color::rgb(0xE8505B);
  Color warning = Color::rgb(0xD9480F);  // e.g. "Voices full"
  Color playhead = Color::rgb(0x2F80ED).with_alpha(28);
  Color playhead_line = Color::rgb(0x2F80ED);

  Color chip = Color::rgb(0xEEEDE9);
  Color chip_active = Color::rgb(0x23221F);

  Color panel_shadow = Color::rgb(0x000000).with_alpha(30);
  Color selection_fill = Color::rgb(0xE8505B).with_alpha(36);
  Color clip_end_shade = Color::rgb(0x23221F).with_alpha(14);
  Color scale_root = Color::rgb(0x2F80ED).with_alpha(22);
  Color tint_base = Color::rgb(0xFFFFFF);  // track colours are mixed towards this for pale fills

  bool dark = false;
  static Theme light() { return Theme(); }
  static Theme dark_theme() {
    Theme t;
    t.dark = true;
    t.background = Color::rgb(0x1B1B1D);
    t.panel = Color::rgb(0x232326);
    t.header = Color::rgb(0x1F1F21);
    t.divider = Color::rgb(0x3A3A3E);
    t.text = Color::rgb(0xECEBE7);
    t.text_secondary = Color::rgb(0x9B9993);
    t.row_white = Color::rgb(0x232326);
    t.row_black = Color::rgb(0x1D1D20);
    // The keys stay a piano's: light keys, a shade dimmer, and black ones.
    t.key_white = Color::rgb(0xD6D4CF);
    t.key_white_shade = Color::rgb(0xC2C0BA);
    t.key_white_edge = Color::rgb(0x8C8A84);
    t.key_black = Color::rgb(0x161617);
    t.key_black_front = Color::rgb(0x2C2B2A);
    t.key_black_highlight = Color::rgb(0x474643);
    t.key_black_edge = Color::rgb(0x000000);
    t.key_shadow = Color::rgb(0x000000).with_alpha(90);
    t.name_column = Color::rgb(0x232326);
    t.name_column_line = Color::rgb(0x2E2E32);
    t.grid_step = Color::rgb(0x2C2C30);
    t.grid_beat = Color::rgb(0x38383C);
    t.grid_bar = Color::rgb(0x58585D);
    t.note = Color::rgb(0x4C8FF0);
    t.note_edge = Color::rgb(0x2F6CC8);
    t.cursor = Color::rgb(0xF0626C);
    t.warning = Color::rgb(0xFF7B3F);
    t.playhead = Color::rgb(0x4C8FF0).with_alpha(40);
    t.playhead_line = Color::rgb(0x5E9CF3);
    t.chip = Color::rgb(0x313135);
    t.chip_active = Color::rgb(0xECEBE7);
    t.panel_shadow = Color::rgb(0x000000).with_alpha(90);
    t.selection_fill = Color::rgb(0xF0626C).with_alpha(60);
    t.clip_end_shade = Color::rgb(0x000000).with_alpha(70);
    t.scale_root = Color::rgb(0x4C8FF0).with_alpha(34);
    t.tint_base = Color::rgb(0x232326);
    return t;
  }

  // One colour per track, clear on a light background.
  static constexpr uint32_t kTrackColours[8] = {0x2F80ED, 0xE8774A, 0x27AE60, 0x9B51E0,
                                                0xEB5757, 0x00A699, 0xD9469B, 0xC09A1E};
  Color track(int t) const { return Color::rgb(kTrackColours[((t % 8) + 8) % 8]); }
};

}  // namespace twill
