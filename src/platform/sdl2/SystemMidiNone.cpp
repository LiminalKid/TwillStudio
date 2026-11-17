// Platforms with no MIDI support yet (the web build): no ports.
#include "platform/sdl2/SystemMidi.h"

namespace twill {

std::unique_ptr<MidiPort> make_system_midi() { return nullptr; }

}  // namespace twill
