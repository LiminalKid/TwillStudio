#include "engine/PlaitsVoice.h"

#include <cmath>
#include <cstring>
#include <new>

#include "plaits/dsp/voice.h"
#include "stmlib/utils/buffer_allocator.h"
#include "stmlib/utils/random.h"

namespace twill {

namespace {

// Plaits computes pitch against its hardware's measured clock (plaits::kCorrectedSampleRate,
// 47872.34 Hz). Running at exactly 48 kHz, everything would be this many semitones sharp.
const float kTuningCorrection = 12.0f * std::log2(48000.0f / plaits::kCorrectedSampleRate);

// Descriptive names, in Plaits' engine order (third_party/eurorack/plaits/dsp/voice.cc).
const char* const kEngineNames[PlaitsVoice::kNumEngines] = {
    "Analog + filter", "Phase distortion", "6-op FM bank 1", "6-op FM bank 2", "6-op FM bank 3",
    "Wave terrain",    "String machine",   "Chiptune",       "Virtual analog", "Waveshaper",
    "Two-op FM",       "Formant",          "Additive",       "Wavetable",      "Chords",
    "Speech",          "Swarm",            "Filtered noise", "Particles",      "Plucked string",
    "Modal resonator", "Bass drum",        "Snare drum",     "Hi-hat",
};

}  // namespace

const char* PlaitsVoice::engine_name(int engine) {
  return engine >= 0 && engine < kNumEngines ? kEngineNames[engine] : "?";
}

void PlaitsVoice::seed_random(uint32_t seed) { stmlib::Random::Seed(seed); }

PlaitsVoice::PlaitsVoice() {
  // Start from zeroed memory: parts of the DSP read state before writing it, and leftover bytes
  // from earlier allocations would otherwise leak faintly into the sound (and make renders differ
  // from run to run).
  void* mem = ::operator new(sizeof(plaits::Voice));
  std::memset(mem, 0, sizeof(plaits::Voice));
  voice_ = new (mem) plaits::Voice;
  std::memset(buffer_, 0, sizeof buffer_);
  stmlib::BufferAllocator allocator(buffer_, sizeof buffer_);
  voice_->Init(&allocator);
}

PlaitsVoice::~PlaitsVoice() {
  voice_->~Voice();
  ::operator delete(voice_);
}

void PlaitsVoice::render_block(const MacroParams& p) {
  plaits::Patch patch = {};
  patch.note = p.note - kTuningCorrection;
  patch.harmonics = p.harmonics;
  patch.timbre = p.timbre;
  patch.morph = p.morph;
  patch.frequency_modulation_amount = p.fm_amount;
  patch.timbre_modulation_amount = p.timbre_amount;
  patch.morph_modulation_amount = p.morph_amount;
  patch.engine = p.engine;
  patch.decay = p.decay;
  patch.lpg_colour = p.lpg_colour;

  plaits::Modulations mod = {};
  mod.trigger_patched = true;
  mod.trigger = gate_ && !retrigger_ ? 1.0f : 0.0f;
  retrigger_ = false;
  rendered_high_ = mod.trigger > 0;
  mod.level_patched = !p.use_lpg;
  mod.level = p.level;

  plaits::Voice::Frame frames[kBlockSize];
  voice_->Render(patch, mod, frames, kBlockSize);
  for (int i = 0; i < kBlockSize; ++i) {
    block_out_[i] = frames[i].out * (1.0f / 32768.0f);
    block_aux_[i] = frames[i].aux * (1.0f / 32768.0f);
  }
  block_pos_ = 0;
}

void PlaitsVoice::render(const MacroParams& p, float* out, float* aux, int frames) {
  for (int i = 0; i < frames; ++i) {
    if (block_pos_ == kBlockSize) render_block(p);
    if (out) out[i] = block_out_[block_pos_];
    if (aux) aux[i] = block_aux_[block_pos_];
    ++block_pos_;
  }
}

}  // namespace twill
