#include "engine/Effects.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

#include "clouds/dsp/frame.h"
// reverb.h uses FloatFrame from frame.h without including it; frame.h is first, above.
#include "clouds/dsp/fx/reverb.h"

namespace twill {

namespace {
constexpr float kRate = 48000;
constexpr int kChorusFrames = 4096;      // 85 ms
constexpr int kDelayFrames = 48000 * 2;  // 2 s
const float kDelayBeats[] = {0.25f, 1.0f / 3, 0.5f, 0.75f, 1, 1.5f, 2};  // 1/16 ... 1/2

void shelf_or_peak(float* b, float* a, int kind, float freq, float gain_db) {
  // RBJ cookbook: kind 0 low shelf, 1 peak (Q 0.7), 2 high shelf.
  const float A = std::pow(10.0f, gain_db / 40);
  const float w = 2 * 3.14159265f * freq / kRate;
  const float cw = std::cos(w), sw = std::sin(w);
  float b0, b1, b2, a0, a1, a2;
  if (kind == 1) {
    const float alpha = sw / (2 * 0.7f);
    b0 = 1 + alpha * A, b1 = -2 * cw, b2 = 1 - alpha * A;
    a0 = 1 + alpha / A, a1 = -2 * cw, a2 = 1 - alpha / A;
  } else {
    const float alpha = sw / 2 * std::sqrt(2.0f);  // shelf slope 1
    const float sA = 2 * std::sqrt(A) * alpha;
    if (kind == 0) {
      b0 = A * ((A + 1) - (A - 1) * cw + sA), b1 = 2 * A * ((A - 1) - (A + 1) * cw), b2 = A * ((A + 1) - (A - 1) * cw - sA);
      a0 = (A + 1) + (A - 1) * cw + sA, a1 = -2 * ((A - 1) + (A + 1) * cw), a2 = (A + 1) + (A - 1) * cw - sA;
    } else {
      b0 = A * ((A + 1) + (A - 1) * cw + sA), b1 = -2 * A * ((A - 1) + (A + 1) * cw), b2 = A * ((A + 1) + (A - 1) * cw - sA);
      a0 = (A + 1) - (A - 1) * cw + sA, a1 = 2 * ((A - 1) - (A + 1) * cw), a2 = (A + 1) - (A - 1) * cw - sA;
    }
  }
  b[0] = b0 / a0, b[1] = b1 / a0, b[2] = b2 / a0;
  a[0] = a1 / a0, a[1] = a2 / a0;
}
}  // namespace

void Effects::ReverbDeleter::operator()(clouds::Reverb* r) const {
  r->~Reverb();
  ::operator delete(r);
}

Effects::Effects()
    : chorus_buf_(size_t(kChorusFrames) * 2, 0.0f),
      delay_buf_(size_t(kDelayFrames) * 2, 0.0f),
      reverb_mem_(16384, 0),
      reverb_frames_(256 * 2, 0.0f) {
  void* mem = ::operator new(sizeof(clouds::Reverb));
  std::memset(mem, 0, sizeof(clouds::Reverb));
  reverb_.reset(new (mem) clouds::Reverb);
  reverb_->Init(reverb_mem_.data());
  reverb_->set_diffusion(0.625f);
  reverb_->set_input_gain(0.2f);
  reverb_->set_amount(1.0f);  // fully wet: it's a send
  std::fill(std::begin(lim_need_), std::end(lim_need_), 1.0f);
  std::fill(std::begin(lim_hold_), std::end(lim_hold_), 1.0f);
}

Effects::~Effects() = default;

void Effects::update_eq(const MixSettings& m) {
  const float g[3] = {m.eq_low, m.eq_mid, m.eq_high};
  const float freq[3] = {120, 1000, 6000};
  for (int i = 0; i < 3; ++i) {
    if (g[i] == eq_gains_[i]) continue;
    eq_gains_[i] = g[i];
    float b[3], a[2];
    shelf_or_peak(b, a, i, freq[i], g[i] * 12);
    eq_[i].b0 = b[0], eq_[i].b1 = b[1], eq_[i].b2 = b[2], eq_[i].a1 = a[0], eq_[i].a2 = a[1];
  }
}

void Effects::master_eq(const MixSettings& m, float* io, int n) {
  if (m.eq_low == 0 && m.eq_mid == 0 && m.eq_high == 0) return;  // flat: leave the signal untouched
  update_eq(m);
  for (int i = 0; i < n; ++i)
    for (int ch = 0; ch < 2; ++ch) {
      float x = io[2 * i + ch];
      for (Biquad& b : eq_) x = b.process(x, ch);
      io[2 * i + ch] = x;
    }
}

void Effects::master_compressor(const MixSettings& m, float* io, int n) {
  if (m.comp_threshold <= 0) {  // off: untouched
    comp_gr_db_ = 0;
    return;
  }
  const float threshold = comp_threshold_db(m.comp_threshold);
  const float slope = 1 - 1 / comp_ratio(m.comp_ratio);
  // Makeup: half the reduction a full-scale signal would get, so it doesn't just get quieter.
  const float makeup = -threshold * slope * 0.5f;
  const float attack = std::exp(-1.0f / (0.010f * 48000));
  const float release = std::exp(-1.0f / (comp_release_seconds(m.comp_release) * 48000));
  // A peak envelope (instant up, falling at the release rate) sets the target reduction; the
  // reduction then rises towards it over the attack time and follows the envelope down.
  float gr = comp_gr_db_, env = comp_env_;
  for (int i = 0; i < n; ++i) {
    const float peak = std::max(std::fabs(io[2 * i]), std::fabs(io[2 * i + 1]));
    env = std::max(peak, env * release);
    const float over = 20 * std::log10(env + 1e-9f) - threshold;
    const float target = over > 0 ? over * slope : 0;
    gr = target > gr ? target + (gr - target) * attack : target;
    const float g = std::pow(10.0f, (makeup - gr) / 20);
    io[2 * i] *= g;
    io[2 * i + 1] *= g;
  }
  comp_gr_db_ = gr;
  comp_env_ = env;
}

void Effects::master_out(const MixSettings& m, float* io, int n) {
  constexpr int L = kLimiterDelay;
  constexpr float kCeiling = 0.966f;  // -0.3 dBFS
  const float release = 1 - std::exp(-1.0f / (0.1f * kRate));     // 100 ms
  const float level_glide = 1 - std::exp(-1.0f / (0.005f * kRate));  // 5 ms
  if (master_gain_ < 0) master_gain_ = m.master;
  for (int i = 0; i < n; ++i) {
    master_gain_ += (m.master - master_gain_) * level_glide;
    const float l = io[2 * i] * master_gain_, r = io[2 * i + 1] * master_gain_;
    // The gain this frame needs, and the least any frame in the look-ahead window needs.
    const float peak = std::max(std::fabs(l), std::fabs(r));
    lim_need_[lim_need_pos_] = peak > kCeiling ? kCeiling / peak : 1.0f;
    lim_need_pos_ = (lim_need_pos_ + 1) % (L + 1);
    float need = 1;
    for (float g : lim_need_) need = std::min(need, g);
    // Down at once, back up over the release time; then averaged over the window, so the gain
    // has eased all the way down by the time the loudest frame leaves the delay line.
    lim_release_ = need < lim_release_ ? need : lim_release_ + (need - lim_release_) * release;
    lim_hold_sum_ += lim_release_ - lim_hold_[lim_pos_];
    lim_hold_[lim_pos_] = lim_release_;
    const float gain = lim_hold_sum_ / L;
    const float out_l = lim_audio_[2 * lim_pos_], out_r = lim_audio_[2 * lim_pos_ + 1];
    lim_audio_[2 * lim_pos_] = l;
    lim_audio_[2 * lim_pos_ + 1] = r;
    if (m.limiter) {
      io[2 * i] = std::max(-1.0f, std::min(1.0f, out_l * gain));
      io[2 * i + 1] = std::max(-1.0f, std::min(1.0f, out_r * gain));
    } else {
      io[2 * i] = std::tanh(out_l);
      io[2 * i + 1] = std::tanh(out_r);
    }
    if (++lim_pos_ == L) {
      lim_pos_ = 0;
      lim_hold_sum_ = 0;  // re-add now and then, so rounding can't drift
      for (float g : lim_hold_) lim_hold_sum_ += g;
    }
  }
}

void Effects::process(const MixSettings& m, float tempo, const float* chorus_in, const float* delay_in,
                      const float* reverb_in, float* out, int n) {
  // Chorus: two taps per side, swept in opposite directions.
  {
    const float rate_hz = 0.1f + m.chorus_rate * 2.9f;
    const float depth = 2 + m.chorus_depth * 10;  // ms
    for (int i = 0; i < n; ++i) {
      chorus_buf_[size_t(2 * chorus_pos_)] = chorus_in[2 * i];
      chorus_buf_[size_t(2 * chorus_pos_ + 1)] = chorus_in[2 * i + 1];
      chorus_phase_ += rate_hz / kRate;
      if (chorus_phase_ >= 1) chorus_phase_ -= 1;
      for (int ch = 0; ch < 2; ++ch) {
        float wet = 0;
        for (int tap = 0; tap < 2; ++tap) {
          const float ph = chorus_phase_ + 0.25f * (ch * 2 + tap);
          const float d_ms = 12 + depth * 0.5f * (1 + std::sin(6.2831853f * ph));
          const float d = d_ms * kRate / 1000;
          float rp = float(chorus_pos_) - d;
          while (rp < 0) rp += kChorusFrames;
          const int i0 = int(rp);
          const float f = rp - i0;
          const int i1 = (i0 + 1) % kChorusFrames;
          wet += chorus_buf_[size_t(2 * i0 + ch)] * (1 - f) + chorus_buf_[size_t(2 * i1 + ch)] * f;
        }
        out[2 * i + ch] += wet * 0.5f;
      }
      chorus_pos_ = (chorus_pos_ + 1) % kChorusFrames;
    }
  }
  // Delay: ping-pong, the time a division of the beat, smoothed so changes glide.
  {
    const int div = std::max(0, std::min(6, int(m.delay_time * 6 + 0.5f)));
    const float target = std::min(float(kDelayFrames - 2), kDelayBeats[div] * 60.0f / std::max(20.0f, tempo) * kRate);
    if (delay_time_ <= 0) delay_time_ = target;
    const float fb = m.delay_feedback * 0.9f;
    for (int i = 0; i < n; ++i) {
      delay_time_ += (target - delay_time_) * 0.0005f;
      float rp = float(delay_pos_) - delay_time_;
      while (rp < 0) rp += kDelayFrames;
      const int i0 = int(rp);
      const float f = rp - i0;
      const int i1 = (i0 + 1) % kDelayFrames;
      float tap[2];
      for (int ch = 0; ch < 2; ++ch) tap[ch] = delay_buf_[size_t(2 * i0 + ch)] * (1 - f) + delay_buf_[size_t(2 * i1 + ch)] * f;
      // Ping-pong: each side feeds the other; the input enters on the left (mono sum).
      for (int ch = 0; ch < 2; ++ch) delay_lp_[ch] += (tap[ch] - delay_lp_[ch]) * 0.35f;
      const float in = 0.5f * (delay_in[2 * i] + delay_in[2 * i + 1]);
      delay_buf_[size_t(2 * delay_pos_)] = in + delay_lp_[1] * fb;
      delay_buf_[size_t(2 * delay_pos_ + 1)] = delay_lp_[0] * fb;
      out[2 * i] += tap[0];
      out[2 * i + 1] += tap[1];
      delay_pos_ = (delay_pos_ + 1) % kDelayFrames;
    }
  }
  // Reverb.
  {
    reverb_->set_time(0.5f + m.reverb_size * 0.48f);
    reverb_->set_lp(0.3f + m.reverb_tone * 0.6f);
    clouds::FloatFrame* frames = reinterpret_cast<clouds::FloatFrame*>(reverb_frames_.data());
    for (int i = 0; i < n; ++i) frames[i] = clouds::FloatFrame{reverb_in[2 * i], reverb_in[2 * i + 1]};
    reverb_->Process(frames, size_t(n));
    for (int i = 0; i < n; ++i) {
      out[2 * i] += frames[i].l;
      out[2 * i + 1] += frames[i].r;
    }
  }
}

}  // namespace twill
