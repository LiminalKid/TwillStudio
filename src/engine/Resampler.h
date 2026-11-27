// Converts the engine's stereo output (always 48 kHz) to a device that runs at another rate (a USB
// device at 44.1 kHz through sndstation's own stream, say), so the music keeps its pitch and tempo.
// A windowed-sinc filter, 32 taps, with its cutoff below the lower of the two Nyquist frequencies so
// nothing folds back; the coefficients come from a table of phases, blended between neighbours.
// Real-time safe once reset: no allocation in process().
#pragma once
#include <cstddef>
#include <vector>

namespace twill {

class Resampler {
 public:
  // Ready to turn `in_rate` into `out_rate` (allocates; call from wherever the rates are learned,
  // the audio thread included, but only when they change). Starts from silence.
  void reset(int in_rate, int out_rate);
  int in_rate() const { return in_rate_; }
  int out_rate() const { return out_rate_; }

  // Fills `frames` stereo frames (interleaved) at the output rate, calling `pull` for input at the
  // input rate as it needs it: pull(buffer, count) writes `count` interleaved stereo frames.
  using Pull = void (*)(void* user, float* buffer, int count);
  void process(float* out, int frames, Pull pull, void* user);

  static constexpr int kHalfTaps = 16;  // taps either side of the point between input frames
  static constexpr int kPhases = 256;   // table rows, between one input frame and the next

 private:
  int in_rate_ = 0, out_rate_ = 0;
  double step_ = 1;               // input frames per output frame
  std::vector<float> table_;      // (kPhases + 1) rows of 2 * kHalfTaps coefficients
  std::vector<float> in_;         // stereo input, from kHalfTaps frames before pos_ on
  size_t have_ = 0;               // frames in in_
  double pos_ = 0;                // where the next output frame falls, in in_'s frames
  static constexpr int kChunk = 256;  // input frames pulled at a time
};

}  // namespace twill
