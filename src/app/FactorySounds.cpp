#include "app/FactorySounds.h"

#include <algorithm>
#include <string>
#include <vector>

#include "app/FactoryData.h"

namespace twill {

namespace {

struct Slot {
  const char* file;  // in samples/Factory/
  float tune, level;
  int choke;
  float decay;
};

// General MIDI from note 36. The toms are recorded at three pitches; the other three tom slots tune
// them up. The open hat is the long hat take with a decay, so it can open up: the recording the
// ride plays in full, in a file of its own so its row reads "Open Hat". Hats choke each other; the
// ride doesn't.
const Slot kKit[kKitSlots] = {
    {"36 Kick.wav", 0, 1.0f, 0, 1},         {"37 FM Perc.wav", 0, 0.7f, 0, 1},
    {"38 Snare.wav", 0, 0.85f, 0, 1},       {"39 Clap.wav", 0, 0.85f, 0, 1},
    {"40 Snare 2.wav", 0, 0.85f, 0, 1},     {"41 Tom Low.wav", 0, 0.8f, 0, 1},
    {"42 Closed Hat.wav", 0, 0.6f, 1, 1},   {"41 Tom Low.wav", 3, 0.8f, 0, 1},
    {"44 Pedal Hat.wav", 0, 0.6f, 1, 1},    {"45 Tom Mid.wav", 0, 0.8f, 0, 1},
    {"46 Open Hat.wav", 0, 0.6f, 1, 0.6f},  {"45 Tom Mid.wav", 3, 0.8f, 0, 1},
    {"48 Tom High.wav", 0, 0.8f, 0, 1},     {"49 Cymbal.wav", 0, 0.5f, 0, 1},
    {"48 Tom High.wav", 3, 0.8f, 0, 1},     {"51 Ride.wav", 0, 0.5f, 0, 1},
};

std::string path_of(const char* file) { return std::string(kFactoryFolder) + "/" + file; }

}  // namespace

int make_factory_sounds(Platform& platform) {
  int written = 0;
  std::vector<uint8_t> existing;
  for (int i = 0; i < kNumFactoryFiles; ++i) {
    const EmbeddedFile& f = kFactoryFiles[i];
    const std::string path = path_of(f.name);
    // Missing, or from another version of the app: the folder is the app's, so it's put right.
    if (platform.read_file(path.c_str(), existing) && existing.size() == f.size &&
        std::equal(existing.begin(), existing.end(), f.data))
      continue;
    if (written == 0) platform.make_dir(kFactoryFolder);
    if (platform.write_file(path.c_str(), std::vector<uint8_t>(f.data, f.data + f.size))) ++written;
  }
  return written;
}

void load_factory_kit(SamplerSettings& s) {
  s.mode = SamplerMode::Kit;
  for (int i = 0; i < kKitSlots; ++i) {
    KitSlot k;
    k.sample = path_of(kKit[i].file);
    k.tune = kKit[i].tune;
    k.level = kKit[i].level;
    k.choke = kKit[i].choke;
    k.decay = kKit[i].decay;
    s.kit[i] = k;
  }
}

Track factory_kit_track() {
  Track t;
  t.name = "Drums";
  t.instrument = InstrumentType::Sampler;
  t.voices = 1;  // per drum: a drum hit again cuts off its last hit, as on a drum machine
  load_factory_kit(t.sampler);
  return t;
}

}  // namespace twill
