// Recording a one-shot sample from an input (design §6, "Recording samples"): the input's level for
// a meter, waiting for the first sound (keeping a moment from before it), the take itself, and the
// take trimmed, faded and normalized, as a WAV. The sound view draws it and takes its buttons.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "platform/Platform.h"

namespace twill {

class Recorder {
 public:
  enum class State { Closed, Ready, Listening, Recording, Review };

  explicit Recorder(Platform& p) : p_(p) {}
  ~Recorder() { close(); }

  // Opens an input (empty: the system's default) and shows its level: Ready. False, with `error`
  // set, if it can't.
  bool open(const std::string& input, std::string& error);
  void close();
  // Each frame: takes in what the input heard since.
  void tick();

  // Ready: waits for a sound (`on_sound`) or records at once. Listening: records at once.
  void start(bool on_sound);
  // Recording: stops, and the take is ready to keep (Review); back to Ready if nothing was heard.
  void stop();
  // Back to Ready, dropping whatever was waiting or recorded.
  void cancel();

  State state() const { return state_; }
  bool open() const { return state_ != State::Closed; }
  int rate() const { return info_.sample_rate; }
  int input_channels() const { return info_.channels; }

  // Settings, applied as the sound comes in: which channel (-1: a mix of all; kStereo: the first
  // two, as a stereo take), and gain in dB.
  static constexpr int kStereo = -2;
  int channel = 0;
  float gain_db = 0;
  bool normalize = true;
  // Monitoring: the input heard as it comes in, while it's open (not while a take is reviewed).
  bool monitor = false;

  // The meter: the level now (dB, falling back slowly) and the peak of the last moment.
  float level_db() const { return level_db_; }
  float peak_db() const { return peak_db_; }
  // The take so far (interleaved, take_channels() of them, at rate()), its length in frames and
  // in seconds, and the loudest of its channels at a frame.
  const std::vector<float>& take() const { return take_; }
  int take_channels() const { return take_ch_; }
  size_t take_frames() const { return take_.size() / size_t(take_ch_); }
  float seconds() const { return rate() > 0 ? float(take_frames()) / float(rate()) : 0; }
  float frame_peak(size_t frame) const;
  // Review: where the kept part starts and ends (frames), and the take as it would be saved.
  size_t keep_start() const { return keep_start_; }
  size_t keep_end() const { return keep_end_; }
  std::vector<float> result() const;
  std::vector<uint8_t> wav() const;

  static constexpr float kMaxSeconds = 30;
  static constexpr float kThresholdDb = -36;  // Listening: a sound starts above this
  static constexpr float kPreRollSeconds = 0.05f;
  static constexpr float kMinGainDb = -12, kMaxGainDb = 36;

 private:
  void take_in(const float* frames, size_t count);
  void find_keep();

  Platform& p_;
  State state_ = State::Closed;
  Platform::CaptureInfo info_;
  std::vector<float> in_;     // from the input, interleaved
  std::vector<float> take_;   // interleaved take_ch_, gain applied
  int take_ch_ = 1;           // 1, or 2 for a stereo take (fixed when it starts)
  std::vector<float> pre_;    // Listening: the last moment, a ring of frames (take_ch_ each)
  size_t pre_at_ = 0;
  bool pre_full_ = false;
  float level_db_ = -96, peak_db_ = -96;
  float peak_hold_ = 0;       // seconds the peak has stood
  size_t keep_start_ = 0, keep_end_ = 0;
};

}  // namespace twill
