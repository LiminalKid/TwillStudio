// Some handhelds' audio chips compress their output in hardware, which makes a beat's levels pump
// (a quiet hit comes up, the hits after it get louder). A music app should sound as mixed, so the
// SDL2 platform turns that off while audio is open and puts it back afterwards.
#pragma once
#include <string>

namespace twill {

class OutputCompressor {
 public:
  ~OutputCompressor() { restore(); }
  // Turns the output compressor off if this device has one we know. Returns a line for the log,
  // or "" if there's nothing to do here.
  std::string disable();
  void restore();  // back to how disable() found it

 private:
  bool changed_ = false;
  unsigned original_ = 0;
};

}  // namespace twill
