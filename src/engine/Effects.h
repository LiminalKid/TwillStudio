// The send effects and the master section (design §6, "Mixer and effects"): chorus, a tempo-synced
// ping-pong delay and a reverb (Clouds' reverb, vendored) on send buses, then a three-band EQ.
// Real-time safe: all memory is allocated up front.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "model/Project.h"

namespace clouds {
class Reverb;
}

namespace twill {

class Effects {
 public:
  Effects();
  ~Effects();

  // In: the three send buses (interleaved stereo, n frames). Out: added to `out`.
  void process(const MixSettings& m, float tempo, const float* chorus_in, const float* delay_in,
               const float* reverb_in, float* out, int n);
  // The master EQ, in place.
  void master_eq(const MixSettings& m, float* io, int n);
  // The master compressor, in place: stereo-linked, 10 ms attack, automatic makeup gain.
  void master_compressor(const MixSettings& m, float* io, int n);
  float gain_reduction_db() const { return comp_gr_db_; }  // the latest, for a meter
  // The last stage, in place: the master level (smoothed, so automating it doesn't click), then a
  // look-ahead peak limiter with its ceiling at -0.3 dBFS, or with the limiter off, a tanh soft
  // clip. Either way the audio is delayed by kLimiterDelay frames, so switching doesn't jump.
  // `boost` multiplies the master level, before the limiter (the device's output boost).
  static constexpr int kLimiterDelay = 64;  // 1.3 ms
  void master_out(const MixSettings& m, float* io, int n, float boost = 1);

 private:
  struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1[2] = {}, z2[2] = {};
    float process(float x, int ch) {
      const float y = b0 * x + z1[ch];
      z1[ch] = b1 * x - a1 * y + z2[ch];
      z2[ch] = b2 * x - a2 * y;
      return y;
    }
  };
  void update_eq(const MixSettings& m);
  float comp_gr_db_ = 0;  // current gain reduction, dB (positive)
  // Master level and limiter.
  float master_gain_ = -1;                 // smoothed; -1 until the first block
  float lim_audio_[2 * kLimiterDelay] = {};  // stereo delay line
  float lim_need_[kLimiterDelay + 1] = {};   // the gain each recent input frame needs
  float lim_hold_[kLimiterDelay] = {};       // the held-and-released gain, for the averaging
  float lim_hold_sum_ = kLimiterDelay;
  float lim_release_ = 1;                  // the gain before averaging
  int lim_pos_ = 0, lim_need_pos_ = 0;
  float comp_env_ = 0;    // the peak envelope it follows

  // Each effect sleeps once nothing has been sent to it for long enough that it has nothing left to
  // play (its ring all silence), and wakes with the next block sent to it.
  // Chorus.
  std::vector<float> chorus_buf_;  // stereo ring
  int chorus_pos_ = 0;
  float chorus_phase_ = 0;
  int chorus_quiet_ = 0;           // frames since anything was sent to it
  // Delay.
  std::vector<float> delay_buf_;   // stereo ring
  int delay_pos_ = 0;
  float delay_lp_[2] = {};
  float delay_time_ = 0;           // smoothed, in samples
  int delay_quiet_ = 0;            // frames since it last wrote anything above -140 dB
  bool delay_asleep_ = false;
  int reverb_quiet_ = 0;           // frames since it last had input or played above -120 dB
  // Reverb, built in zeroed memory: it reads some of its state before setting it.
  struct ReverbDeleter {
    void operator()(clouds::Reverb* r) const;
  };
  std::unique_ptr<clouds::Reverb, ReverbDeleter> reverb_;
  std::vector<uint16_t> reverb_mem_;
  std::vector<float> reverb_frames_;
  // EQ.
  Biquad eq_[3];
  float eq_gains_[3] = {99, 99, 99};
};

}  // namespace twill
