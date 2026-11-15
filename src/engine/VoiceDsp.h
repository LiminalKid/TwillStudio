// Building blocks for voices: the amp envelope and the filter and drive every instrument shares
// (design §6, "Instruments"). Real-time safe: no allocation, no locks.
#pragma once
#include <algorithm>
#include <cmath>

#include "model/Project.h"
#include "stmlib/dsp/filter.h"

namespace twill {

// ADSR with linear attack and exponential decay and release, updated once per block and ramped
// across it.
class Adsr {
 public:
  enum Stage { kIdle, kAttack, kDecay, kRelease };

  void gate_on() { stage_ = kAttack; }
  // Legato: a releasing envelope goes back to its decay/sustain from where it is.
  void resume() {
    if (stage_ == kRelease && force_release_ == 0) stage_ = kDecay;
  }
  void gate_off() {
    if (stage_ != kIdle) stage_ = kRelease;
  }
  void kill(float seconds) {  // a fast release, for chokes and stolen voices
    if (stage_ == kIdle) return;
    stage_ = kRelease;
    force_release_ = seconds;
  }
  void reset() {
    stage_ = kIdle;
    level_ = 0;
    from_ = -1;
    force_release_ = 0;
  }
  // A new note on a sounding voice: attack from zero, but the first block ramps down from the
  // current level rather than jumping, so it doesn't click.
  void retrigger() {
    const float was = level_;
    reset();
    from_ = was;
    stage_ = kAttack;
  }
  bool idle() const { return stage_ == kIdle; }
  float level() const { return level_; }

  // Advances `n` samples; returns the level at the start of the block (ramp from it to level()).
  float advance(const ToneSettings& t, int n, float sample_rate) {
    const float start = from_ >= 0 ? from_ : level_;
    from_ = -1;
    const float dt = n / sample_rate;
    switch (stage_) {
      case kIdle:
        break;
      case kAttack: {
        level_ += dt / std::max(1e-4f, env_seconds(t.attack));
        if (level_ >= 1) {
          level_ = 1;
          stage_ = kDecay;
        }
        break;
      }
      case kDecay: {
        // Towards the sustain level, about 99% of the way in the decay time.
        const float k = std::exp(-4.6f * dt / env_seconds(t.decay));
        level_ = t.sustain + (level_ - t.sustain) * k;
        break;
      }
      case kRelease: {
        const float seconds = force_release_ > 0 ? force_release_ : env_seconds(t.release);
        level_ *= std::exp(-4.6f * dt / seconds);
        if (level_ < 1e-4f) {
          level_ = 0;
          stage_ = kIdle;
          force_release_ = 0;
        }
        break;
      }
    }
    return start;
  }

 private:
  Stage stage_ = kIdle;
  float level_ = 0;
  float from_ = -1;  // the next block ramps from here (retrigger), when >= 0
  float force_release_ = 0;
};

// Filter (state-variable, 12 dB/octave) and drive, on a stereo pair.
class ToneStage {
 public:
  void reset() {
    for (auto& f : svf_) f.Init();
  }
  void set(const ToneSettings& t, float sample_rate) {
    type_ = t.filter;
    drive_ = t.drive;
    if (type_ == FilterType::Off) return;
    const float f = std::min(0.45f, cutoff_hz(t.cutoff) / sample_rate);
    const float q = 0.5f * std::pow(40.0f, t.resonance);  // 0.5 to 20
    for (auto& s : svf_) s.set_f_q<stmlib::FREQUENCY_FAST>(f, q);
  }
  void process(float* l, float* r, int n) {
    if (type_ != FilterType::Off) {
      for (int i = 0; i < n; ++i) {
        switch (type_) {
          case FilterType::LowPass:
            l[i] = svf_[0].Process<stmlib::FILTER_MODE_LOW_PASS>(l[i]);
            r[i] = svf_[1].Process<stmlib::FILTER_MODE_LOW_PASS>(r[i]);
            break;
          case FilterType::BandPass:
            l[i] = svf_[0].Process<stmlib::FILTER_MODE_BAND_PASS_NORMALIZED>(l[i]);
            r[i] = svf_[1].Process<stmlib::FILTER_MODE_BAND_PASS_NORMALIZED>(r[i]);
            break;
          case FilterType::HighPass:
            l[i] = svf_[0].Process<stmlib::FILTER_MODE_HIGH_PASS>(l[i]);
            r[i] = svf_[1].Process<stmlib::FILTER_MODE_HIGH_PASS>(r[i]);
            break;
          case FilterType::Off:
            break;
        }
      }
    }
    if (drive_ > 0.001f) {
      // Soft clipping, gain-compensated so the level stays about the same as drive goes up.
      const float g = 1 + drive_ * 9;
      const float makeup = 1.0f / std::tanh(g);
      for (int i = 0; i < n; ++i) {
        l[i] = std::tanh(l[i] * g) * makeup;
        r[i] = std::tanh(r[i] * g) * makeup;
      }
    }
  }

 private:
  stmlib::Svf svf_[2];
  FilterType type_ = FilterType::Off;
  float drive_ = 0;
};

// 4-point Hermite interpolation.
inline float hermite4(float xm1, float x0, float x1, float x2, float t) {
  const float c = (x1 - xm1) * 0.5f;
  const float v = x0 - x1;
  const float w = c + v;
  const float a = w + v + (x2 - x0) * 0.5f;
  const float b = w + a;
  return ((a * t - b) * t + c) * t + x0;
}

}  // namespace twill
