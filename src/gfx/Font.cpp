#include "gfx/Font.h"

#include <vector>

namespace twill {

const Font& Font::get(FontFace face, float pixel_size) {
  // Built once; kFonts is constant data, so this is the only state.
  static std::vector<Font> fonts = [] {
    std::vector<Font> v;
    for (int i = 0; i < kNumFonts; ++i) v.emplace_back(&kFonts[i]);
    return v;
  }();
  const Font* best = nullptr;
  for (const Font& f : fonts) {
    if (f.d_->face != face) continue;
    if (!best) best = &f;
    if (f.d_->pixel_size <= pixel_size + 0.5f) best = &f;  // sizes are generated in ascending order
  }
  return *best;
}

const GlyphInfo* Font::glyph(char c) const {
  int code = static_cast<unsigned char>(c);
  if (code < d_->first || code > d_->last) code = '?';
  return &d_->glyphs[code - d_->first];
}

float Font::measure(const char* text, float spacing) const {
  float w = 0;
  for (const char* p = text; *p; ++p) w += glyph(*p)->advance + (p[1] ? spacing : 0);
  return w;
}

}  // namespace twill
