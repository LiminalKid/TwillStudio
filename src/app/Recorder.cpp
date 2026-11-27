#include "app/Recorder.h"

#include <algorithm>
#include <cmath>

#include "engine/Sample.h"

namespace twill {

namespace {
float to_db(float v) { return v > 1e-5f ? 20 * std::log10(v) : -100; }
float from_db(float db) { return std::pow(10.0f, db / 20); }
}  // namespace

bool Recorder::open(const std::string& input, std::string& error) {
  close();
  Platform::CaptureInfo info;
  if (!p_.capture_open(input, info, error)) return false;
  info_ = info;
  if (info_.channels < 1) info_.channels = 1;
  state_ = State::Ready;
  take_.clear();
  in_.clear();
  level_db_ = peak_db_ = -100;
  return true;
}

void Recorder::close() {
  if (state_ == State::Closed) return;
  p_.capture_monitor(false, 0, 0);
  p_.capture_close();
  state_ = State::Closed;
  take_.clear();
  take_.shrink_to_fit();
}

void Recorder::start(bool on_sound) {
  if (state_ != State::Ready && state_ != State::Listening) return;
  if (state_ == State::Ready) take_ch_ = channel == kStereo && info_.channels >= 2 ? 2 : 1;
  take_.clear();
  if (on_sound && state_ == State::Ready) {
    pre_.assign(size_t(std::max(1.0f, kPreRollSeconds * float(rate()))) * size_t(take_ch_), 0.0f);
    pre_at_ = 0;
    pre_full_ = false;
    state_ = State::Listening;
    return;
  }
  if (state_ == State::Listening) {  // at once: with what was waiting
    if (pre_full_) take_.insert(take_.end(), pre_.begin() + long(pre_at_), pre_.end());
    take_.insert(take_.end(), pre_.begin(), pre_.begin() + long(pre_at_));
  }
  take_.reserve(size_t(kMaxSeconds * float(rate())) * size_t(take_ch_) + pre_.size());
  state_ = State::Recording;
}

void Recorder::stop() {
  if (state_ == State::Listening) return cancel();
  if (state_ != State::Recording) return;
  if (take_.empty()) return cancel();
  find_keep();
  state_ = State::Review;
}

void Recorder::cancel() {
  if (state_ == State::Closed) return;
  take_.clear();
  state_ = State::Ready;
}

void Recorder::tick() {
  if (state_ == State::Closed) return;
  p_.capture_monitor(monitor && state_ != State::Review, channel, from_db(gain_db));
  in_.clear();
  const size_t frames = p_.capture_read(in_);
  // Review: the input is still read, so it doesn't pile up, and the meter keeps falling.
  take_in(in_.data(), frames);
}

void Recorder::take_in(const float* in, size_t frames) {
  const int ch = info_.channels;
  const bool stereo = channel == kStereo && ch >= 2;
  const int use = channel >= ch ? ch - 1 : channel;
  const float g = from_db(gain_db), thr = from_db(kThresholdDb);
  const size_t max_len = size_t(kMaxSeconds * float(rate()));
  float block_peak = 0;
  for (size_t f = 0; f < frames; ++f) {
    const float* fr = in + f * size_t(ch);
    float x[2] = {0, 0};
    if (stereo) {
      x[0] = fr[0];
      x[1] = fr[1];
    } else if (use < 0) {  // the mix (Stereo, with a one-channel input, is the mix too)
      for (int c = 0; c < ch; ++c) x[0] += fr[c];
      x[0] /= float(ch);
      x[1] = x[0];
    } else {
      x[0] = x[1] = fr[use];
    }
    x[0] *= g;
    x[1] *= g;
    const float peak = std::max(std::fabs(x[0]), std::fabs(x[1]));
    block_peak = std::max(block_peak, peak);
    const size_t n = size_t(take_ch_);
    if (state_ == State::Listening) {
      if (peak >= thr) {
        start(false);  // from the moment kept before it
        take_.insert(take_.end(), x, x + n);
        continue;
      }
      for (size_t c = 0; c < n; ++c) pre_[pre_at_ + c] = x[c];
      pre_at_ += n;
      if (pre_at_ >= pre_.size()) pre_at_ = 0, pre_full_ = true;
    } else if (state_ == State::Recording) {
      take_.insert(take_.end(), x, x + n);
      if (take_frames() >= max_len) {
        stop();
      }
    }
  }
  // The meter: up at once, down about 20 dB a second; the peak stands a second and a half.
  const float dt = rate() > 0 ? float(frames) / float(rate()) : 0;
  const float db = to_db(block_peak);
  level_db_ = std::max(db, level_db_ - 20 * dt);
  peak_hold_ += dt;
  if (db >= peak_db_ || peak_hold_ > 1.5f) {
    peak_db_ = std::max(db, level_db_);
    if (db >= peak_db_) peak_hold_ = 0;
  }
}

float Recorder::frame_peak(size_t frame) const {
  const size_t n = size_t(take_ch_), i = frame * n;
  if (i + n > take_.size()) return 0;
  return n == 2 ? std::max(std::fabs(take_[i]), std::fabs(take_[i + 1])) : std::fabs(take_[i]);
}

// What to keep: from just before the sound starts (30 dB under the peak) to where it has died away
// (50 dB under it, or into the input's own noise), with a short fade after. Frames: in stereo, the
// louder channel.
void Recorder::find_keep() {
  const size_t frames = take_frames();
  float peak = 0;
  for (size_t i = 0; i < frames; ++i) peak = std::max(peak, frame_peak(i));
  keep_start_ = 0;
  keep_end_ = frames;
  if (peak <= 1e-5f) return;
  const float on = peak * from_db(-30);
  size_t first = 0;
  while (first < frames && frame_peak(first) < on) ++first;
  double noise = 0;
  for (size_t i = 0; i < first; ++i) noise += double(frame_peak(i)) * frame_peak(i);
  noise = first ? std::sqrt(noise / double(first)) : 0;
  const float off = std::max(peak * from_db(-50), float(noise) * 2.5f);
  size_t last = frames;
  while (last > first && frame_peak(last - 1) < off) --last;
  const size_t lead = size_t(0.005f * float(rate())), fade = size_t(0.01f * float(rate()));
  keep_start_ = first > lead ? first - lead : 0;
  keep_end_ = std::min(frames, last + fade);
  if (keep_end_ <= keep_start_) keep_start_ = 0, keep_end_ = frames;
}

std::vector<float> Recorder::result() const {
  const size_t n = size_t(take_ch_);
  if (keep_end_ <= keep_start_ || keep_end_ > take_frames()) return {};
  std::vector<float> out(take_.begin() + long(keep_start_ * n), take_.begin() + long(keep_end_ * n));
  const size_t frames = out.size() / n;
  // A short fade in where it was cut, and out at the end, so neither clicks.
  const size_t in_len = keep_start_ > 0 ? std::min(frames, size_t(0.002f * float(rate()))) : 0;
  const size_t out_len = std::min(frames, size_t(0.01f * float(rate())));
  for (size_t i = 0; i < in_len; ++i)
    for (size_t c = 0; c < n; ++c) out[i * n + c] *= float(i) / float(in_len);
  for (size_t i = 0; i < out_len; ++i)
    for (size_t c = 0; c < n; ++c) out[(frames - 1 - i) * n + c] *= float(i) / float(out_len);
  if (normalize) {
    float peak = 0;
    for (float x : out) peak = std::max(peak, std::fabs(x));
    if (peak > 1e-5f) {
      const float g = from_db(-1) / peak;
      for (float& x : out) x *= g;
    }
  }
  return out;
}

std::vector<uint8_t> Recorder::wav() const {
  const std::vector<float> r = result();
  return encode_wav16_channels(r.data(), int(r.size() / size_t(take_ch_)), rate(), take_ch_);
}

}  // namespace twill
