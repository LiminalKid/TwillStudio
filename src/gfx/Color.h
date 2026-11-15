// Colours are stored as 0xAARRGGBB, the same layout as the canvas pixels.
#pragma once
#include <cstdint>

namespace twill {

struct Color {
  uint32_t argb;

  constexpr uint8_t a() const { return argb >> 24; }
  constexpr uint8_t r() const { return (argb >> 16) & 0xff; }
  constexpr uint8_t g() const { return (argb >> 8) & 0xff; }
  constexpr uint8_t b() const { return argb & 0xff; }

  static constexpr Color rgb(uint32_t rgb) { return Color{0xff000000u | rgb}; }
  constexpr Color with_alpha(uint8_t alpha) const {
    return Color{(argb & 0x00ffffffu) | (uint32_t(alpha) << 24)};
  }
  // Mix towards `o` by t (0-255).
  constexpr Color mix(Color o, int t) const {
    return Color{(uint32_t(a() + (o.a() - a()) * t / 255) << 24) |
                 (uint32_t(r() + (o.r() - r()) * t / 255) << 16) |
                 (uint32_t(g() + (o.g() - g()) * t / 255) << 8) |
                 uint32_t(b() + (o.b() - b()) * t / 255)};
  }
};

}  // namespace twill
