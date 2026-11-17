// Colour tokens (design §7). The look is paper and ink: cream paper, near-black ink, firm rules and
// muted track colours, with clips as a pale fill of their track's colour and notes in a deep shade
// of it. The defaults are the light theme; dark_theme() is the same design in cool graphite with
// off-white ink, chosen in the app's settings.
#pragma once
#include "gfx/Color.h"

namespace twill {

struct Theme {
  Color background = Color::rgb(0xF5EFE1);  // the paper
  Color panel = Color::rgb(0xFAF6EC);       // a sheet lying on the paper: panels, track headers
  Color header = Color::rgb(0xF5EFE1);      // the top bar and rulers
  Color divider = Color::rgb(0xD3C9B3);
  Color text = Color::rgb(0x151412);        // the ink
  Color text_secondary = Color::rgb(0x71695A);

  Color row_white = Color::rgb(0xF5EFE1);
  Color row_black = Color::rgb(0xEFE8D7);
  Color key_white = Color::rgb(0xFBF7EE);
  Color key_white_shade = Color::rgb(0xF1EBDD);  // just above a white key's bottom edge
  Color key_white_edge = Color::rgb(0xA39A8A);   // the lines between keys
  Color key_black = Color::rgb(0x151412);
  Color key_black_front = Color::rgb(0x2E2C28);  // the sloped front end
  Color key_black_highlight = Color::rgb(0x4A4741);
  Color key_black_edge = Color::rgb(0x000000);
  Color key_shadow = Color::rgb(0x000000).with_alpha(30);
  // The roll's left column when it lists names instead of keys (kit sounds, notes of a scale).
  Color name_column = Color::rgb(0xFAF6EC);
  Color name_column_line = Color::rgb(0xD3C9B3);
  Color grid_step = Color::rgb(0xE3DBC8);
  Color grid_beat = Color::rgb(0xD3C9B3);
  Color grid_bar = Color::rgb(0xA99D84);

  Color note = Color::rgb(0x2C4C9E);       // the accent: links and highlights
  Color note_edge = Color::rgb(0x1F3A7D);
  Color cursor = Color::rgb(0xC8423A);     // messages, rubber-band selection
  Color warning = Color::rgb(0xC4501A);    // e.g. "Voices full"
  Color playhead = Color::rgb(0x151412).with_alpha(18);
  Color playhead_line = Color::rgb(0x151412);

  Color chip = Color::rgb(0xEFE8D7);         // a quiet highlight, e.g. the current track's header
  Color chip_active = Color::rgb(0x151412);  // a selected row or the current tab: an ink block
  Color chip_active_text = Color::rgb(0xF5EFE1);

  // The hint bar is ink in both themes, with its buttons drawn as paper keycaps.
  Color hint_bar = Color::rgb(0x151412);
  Color hint_text = Color::rgb(0xF5EFE1);
  Color keycap = Color::rgb(0xF5EFE1);
  Color keycap_text = Color::rgb(0x151412);

  int shadow_strength = 56;  // darkest alpha of the soft shadows panels and headers cast
  Color selection_fill = Color::rgb(0xC8423A).with_alpha(36);
  Color clip_end_shade = Color::rgb(0x151412).with_alpha(14);
  Color scale_root = Color::rgb(0x6F8ED6).with_alpha(30);
  Color tint_base = Color::rgb(0xF5EFE1);  // track colours are mixed towards this for pale fills

  bool dark = false;
  static Theme light() { return Theme(); }
  static Theme dark_theme() {
    Theme t;
    t.dark = true;
    t.background = Color::rgb(0x1B1B1D);
    t.panel = Color::rgb(0x26262A);
    t.header = Color::rgb(0x1F1F21);
    t.divider = Color::rgb(0x36363A);
    t.text = Color::rgb(0xECEBE7);
    t.text_secondary = Color::rgb(0x9B9993);
    t.row_white = Color::rgb(0x1B1B1D);
    t.row_black = Color::rgb(0x1F1F22);
    // The keys stay a piano's: light keys, a shade dimmer, and black ones.
    t.key_white = Color::rgb(0xD6D4CF);
    t.key_white_shade = Color::rgb(0xC6C4BE);
    t.key_white_edge = Color::rgb(0x6A6964);
    t.key_black = Color::rgb(0x0E0E10);
    t.key_black_front = Color::rgb(0x252527);
    t.key_black_highlight = Color::rgb(0x3E3E41);
    t.key_black_edge = Color::rgb(0x000000);
    t.key_shadow = Color::rgb(0x000000).with_alpha(90);
    t.name_column = Color::rgb(0x26262A);
    t.name_column_line = Color::rgb(0x36363A);
    t.grid_step = Color::rgb(0x2A2A2E);
    t.grid_beat = Color::rgb(0x36363A);
    t.grid_bar = Color::rgb(0x58585D);
    t.note = Color::rgb(0x8FAAE8);
    t.note_edge = Color::rgb(0x6F8ED6);
    t.cursor = Color::rgb(0xF0726A);
    t.warning = Color::rgb(0xFF8B4F);
    t.playhead = Color::rgb(0xECEBE7).with_alpha(18);
    t.playhead_line = Color::rgb(0xECEBE7);
    t.chip = Color::rgb(0x2A2A2E);
    t.chip_active = Color::rgb(0xECEBE7);
    t.chip_active_text = Color::rgb(0x1B1B1D);
    t.hint_bar = Color::rgb(0x0E0E10);
    t.hint_text = Color::rgb(0xECEBE7);
    t.keycap = Color::rgb(0xECEBE7);
    t.keycap_text = Color::rgb(0x1B1B1D);
    t.shadow_strength = 128;
    t.selection_fill = Color::rgb(0xF0726A).with_alpha(60);
    t.clip_end_shade = Color::rgb(0x000000).with_alpha(70);
    t.scale_root = Color::rgb(0x6F8ED6).with_alpha(40);
    t.tint_base = Color::rgb(0x1B1B1D);
    return t;
  }

  // One colour per track, muted to sit on paper: blue, orange, green, purple, red, teal, pink, gold
  // (the same hues as before the paper look, so a project's tracks keep their colours). Each comes
  // as the swatch, a pale fill and a deep shade for notes on the pale fill.
  static constexpr uint32_t kTrackColours[8] = {0x6F8ED6, 0xE58A55, 0x7DAE88, 0xA88AD6,
                                                0xD9625F, 0x58B0A5, 0xDE88AC, 0xE2B649};
  static constexpr uint32_t kTrackPale[8] = {0xC2D0F0, 0xF5CDB3, 0xC8DEC9, 0xDACDF0,
                                             0xF2C0BD, 0xB7E0DA, 0xF3CBDA, 0xF4DD9C};
  static constexpr uint32_t kTrackDeep[8] = {0x2C4C9E, 0xA04E1C, 0x2C6539, 0x57379A,
                                             0x9E2B2A, 0x1C6A60, 0x9C365F, 0x7F5C0C};
  static int wrap(int t) { return ((t % 8) + 8) % 8; }
  Color track(int t) const { return Color::rgb(kTrackColours[wrap(t)]); }
  // A clip's fill: pale on paper; in the dark, a dark tint of the colour.
  Color track_fill(int t) const {
    return dark ? background.mix(track(t), 77) : Color::rgb(kTrackPale[wrap(t)]);
  }
  // Notes and other marks drawn on a clip's fill: deep on paper, pale in the dark.
  Color track_ink(int t) const {
    return Color::rgb(dark ? kTrackPale[wrap(t)] : kTrackDeep[wrap(t)]);
  }
};

}  // namespace twill
