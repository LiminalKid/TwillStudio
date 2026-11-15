#include "model/Scale.h"

#include <initializer_list>

namespace twill {

namespace {
struct ScaleDef {
  const char* name;
  uint16_t mask;  // bit n set: n semitones above the key is in the scale
};
constexpr uint16_t bits(std::initializer_list<int> steps) {
  uint16_t m = 0;
  for (int s : steps) m |= uint16_t(1u << s);
  return m;
}
const ScaleDef kScales[] = {
    {"Chromatic", 0x0fff},
    {"Major", bits({0, 2, 4, 5, 7, 9, 11})},
    {"Minor", bits({0, 2, 3, 5, 7, 8, 10})},
    {"Dorian", bits({0, 2, 3, 5, 7, 9, 10})},
    {"Phrygian", bits({0, 1, 3, 5, 7, 8, 10})},
    {"Lydian", bits({0, 2, 4, 6, 7, 9, 11})},
    {"Mixolydian", bits({0, 2, 4, 5, 7, 9, 10})},
    {"Harmonic minor", bits({0, 2, 3, 5, 7, 8, 11})},
    {"Major pentatonic", bits({0, 2, 4, 7, 9})},
    {"Minor pentatonic", bits({0, 3, 5, 7, 10})},
    {"Blues", bits({0, 3, 5, 6, 7, 10})},
};
const char* const kKeys[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
}  // namespace

const char* scale_name(ScaleType s) {
  int i = int(s);
  return i >= 0 && i < int(ScaleType::kCount) ? kScales[i].name : "?";
}

const char* key_name(int key) { return kKeys[((key % 12) + 12) % 12]; }

bool in_scale(int note, int key, ScaleType s) {
  int i = int(s);
  if (i < 0 || i >= int(ScaleType::kCount)) return true;
  int degree = (((note - key) % 12) + 12) % 12;
  return (kScales[i].mask >> degree) & 1u;
}

}  // namespace twill
