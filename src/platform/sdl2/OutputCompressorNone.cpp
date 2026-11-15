// Platforms whose output isn't compressed behind our back (or that we can't reach): nothing to do.
#include "platform/sdl2/OutputCompressor.h"

namespace twill {

std::string OutputCompressor::disable() { return ""; }
void OutputCompressor::restore() {
  changed_ = false;
  original_ = 0;
}

}  // namespace twill
