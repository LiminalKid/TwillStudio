// The factory kit: drum samples built into the app (assets/factory/, recorded from an analog drum
// machine), written to samples/Factory/ on first launch so the sampler and the browser treat them
// like any other sample.
#pragma once
#include "model/Project.h"
#include "platform/Platform.h"

namespace twill {

constexpr const char* kFactoryFolder = "samples/Factory";

// Writes any factory samples that are missing or differ from the app's own (an older version's).
// Returns how many it wrote.
int make_factory_sounds(Platform& platform);
// Fills a kit's sixteen slots with the factory kit in General MIDI order (kick on 36, snare on 38,
// closed hat on 42...), with its levels, choke groups and decays.
void load_factory_kit(SamplerSettings& s);
// A track playing the factory kit, as new songs start with.
Track factory_kit_track();

}  // namespace twill
