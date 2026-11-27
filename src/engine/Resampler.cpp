#include "engine/Resampler.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace twill {

namespace {
constexpr double kPi = 3.14159265358979323846;

// The Kaiser window's I0, by its series.
double bessel_i0(double x) {
  double sum = 1, term = 1;
  for (int k = 1; k < 32; ++k) {
    term *= (x / (2 * k)) * (x / (2 * k));
    sum += term;
    if (term < sum * 1e-12) break;
  }
  return sum;
}
}  // namespace

void Resampler::reset(int in_rate, int out_rate) {
  in_rate_ = std::max(1, in_rate);
  out_rate_ = std::max(1, out_rate);
  step_ = double(in_rate_) / double(out_rate_);
  // Cutoff, as a fraction of the input's Nyquist: below the output's when going down, with room for
  // the filter's slope (about 20 kHz at 44.1 kHz out).
  const double cutoff = std::min(1.0, 1.0 / step_) * 0.91;
  const double beta = 8.0, norm = bessel_i0(beta);
  const int taps = 2 * kHalfTaps;
  table_.assign(size_t(kPhases + 1) * size_t(taps), 0.0f);
  for (int p = 0; p <= kPhases; ++p) {
    const double frac = double(p) / kPhases;  // the output point, this far past input frame 0
    float* row = &table_[size_t(p) * size_t(taps)];
    double sum = 0;
    for (int k = 0; k < taps; ++k) {
      const double x = double(k - (kHalfTaps - 1)) - frac;  // tap k's input frame, from the point
      const double s = x == 0 ? 1.0 : std::sin(kPi * cutoff * x) / (kPi * cutoff * x);
      const double w = x / kHalfTaps;
      const double win = std::fabs(w) >= 1 ? 0.0 : bessel_i0(beta * std::sqrt(1 - w * w)) / norm;
      row[k] = float(s * win);
      sum += s * win;
    }
    for (int k = 0; k < taps; ++k) row[k] = float(row[k] / sum);  // unity gain at every phase
  }
  // Room for what one block can ask for, at most, plus the taps; it grows if a block is longer.
  in_.assign(size_t(8192 + 2 * taps + kChunk) * 2, 0.0f);
  have_ = size_t(kHalfTaps);  // silence before the start
  pos_ = double(kHalfTaps - 1);
}

void Resampler::process(float* out, int frames, Pull pull, void* user) {
  const int taps = 2 * kHalfTaps;
  for (int i = 0; i < frames; ++i) {
    const size_t base = size_t(pos_);  // the input frame at or before the point
    // Each tap from base - (kHalfTaps - 1) to base + kHalfTaps must be in hand.
    while (base + size_t(kHalfTaps) >= have_) {
      if ((have_ + kChunk) * 2 > in_.size()) in_.resize((have_ + kChunk) * 2 * 2, 0.0f);  // a very long block
      pull(user, &in_[have_ * 2], kChunk);
      have_ += size_t(kChunk);
    }
    const double f = (pos_ - double(base)) * kPhases;
    const int p = std::min(kPhases - 1, int(f));
    const float t = float(f - p);
    const float* r0 = &table_[size_t(p) * size_t(taps)];
    const float* r1 = r0 + taps;
    const float* x = &in_[(base - size_t(kHalfTaps - 1)) * 2];
    float l = 0, r = 0;
    for (int k = 0; k < taps; ++k) {
      const float c = r0[k] + (r1[k] - r0[k]) * t;
      l += c * x[2 * k];
      r += c * x[2 * k + 1];
    }
    out[2 * i] = l;
    out[2 * i + 1] = r;
    pos_ += step_;
  }
  // Drop what no tap will need again, keeping kHalfTaps frames of history.
  const size_t base = size_t(pos_);
  if (base > size_t(kHalfTaps)) {
    const size_t drop = base - size_t(kHalfTaps - 1);
    std::memmove(in_.data(), in_.data() + drop * 2, (have_ - drop) * 2 * sizeof(float));
    have_ -= drop;
    pos_ -= double(drop);
  }
}

}  // namespace twill
