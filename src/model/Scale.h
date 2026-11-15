// Keys and scales for the piano roll's scale mode (design §7).
#pragma once
#include <cstdint>

namespace twill {

enum class ScaleType : uint8_t {
  Chromatic,
  Major,
  Minor,
  Dorian,
  Phrygian,
  Lydian,
  Mixolydian,
  HarmonicMinor,
  MajorPentatonic,
  MinorPentatonic,
  Blues,
  kCount
};

const char* scale_name(ScaleType s);
const char* key_name(int key);  // 0 = C ... 11 = B
// True if `note` belongs to the scale on `key`.
bool in_scale(int note, int key, ScaleType s);

}  // namespace twill
