// Pre-rendered fonts (see tools/mkfonts.py). A Font is one face at one pixel size.
#pragma once
#include <cstdint>

namespace twill {

enum class FontFace : uint8_t { Regular, SemiBold };

struct GlyphInfo {
  int16_t atlas_x, atlas_y;  // top-left of the glyph in the atlas
  int16_t w, h;              // glyph bitmap size; 0 for blank glyphs such as space
  int16_t ox, oy;            // bitmap offset from the pen position on the baseline
  float advance;             // pen advance in pixels
};

struct FontData {
  FontFace face;
  int pixel_size;
  int ascent, descent;
  int first, last;           // character range covered
  int atlas_width, atlas_height;
  const GlyphInfo* glyphs;
  const uint8_t* atlas;      // 8-bit alpha, atlas_width * atlas_height
};

extern const FontData kFonts[];
extern const int kNumFonts;

class Font {
 public:
  // The generated font closest to `pixel_size` (rounding down when between two sizes).
  static const Font& get(FontFace face, float pixel_size);

  int ascent() const { return d_->ascent; }
  int descent() const { return d_->descent; }
  int line_height() const { return d_->ascent + d_->descent; }
  int pixel_size() const { return d_->pixel_size; }
  const GlyphInfo* glyph(char c) const;
  const uint8_t* atlas() const { return d_->atlas; }
  int atlas_width() const { return d_->atlas_width; }
  float measure(const char* text) const;

  explicit Font(const FontData* d) : d_(d) {}

 private:
  const FontData* d_;
};

}  // namespace twill
