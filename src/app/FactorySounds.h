// Sounds the app makes for itself on first launch, so the sampler has something to play: drums
// and a few pitched notes rendered from the macro oscillator into samples/Factory/.
#pragma once
#include <string>
#include <vector>

#include "platform/Platform.h"

namespace twill {

constexpr const char* kFactoryFolder = "samples/Factory";

// The factory sounds' paths (relative to the data folder), in kit order.
const std::vector<std::string>& factory_sound_paths();
// Renders any factory sounds that are missing. Returns how many it wrote.
int make_factory_sounds(Platform& platform);

}  // namespace twill
