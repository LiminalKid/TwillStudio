// The system's MIDI devices for the SDL2 platform: ALSA raw MIDI devices on Linux, CoreMIDI on
// macOS. Null where there's no implementation yet (Windows).
#pragma once
#include <memory>

#include "platform/Midi.h"

namespace twill {

std::unique_ptr<MidiPort> make_system_midi();

}  // namespace twill
