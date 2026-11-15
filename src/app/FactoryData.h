// The factory kit's samples, built into the app from assets/factory/ (cmake/embed.cmake).
#pragma once
#include <cstddef>

namespace twill {

struct EmbeddedFile {
  const char* name;  // file name, e.g. "36 Kick.wav"
  const unsigned char* data;
  size_t size;
};
extern const EmbeddedFile kFactoryFiles[];
extern const int kNumFactoryFiles;

}  // namespace twill
