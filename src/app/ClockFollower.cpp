#include "app/ClockFollower.h"

#include <algorithm>
#include <cmath>

namespace twill {

namespace {
constexpr size_t kWindow = 96;         // pulses averaged over: four beats
constexpr size_t kShort = 24;          // one beat, to notice a tempo change
constexpr int64_t kRestartUs = 250000;  // a longer gap starts a new estimate

// The slope, in microseconds per pulse, of a least-squares line through times[from..end).
double slope(const std::vector<int64_t>& t, size_t from) {
  const size_t n = t.size() - from;
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  for (size_t i = 0; i < n; ++i) {
    const double x = double(i), y = double(t[from + i] - t[from]);
    sx += x, sy += y, sxx += x * x, sxy += x * y;
  }
  const double d = n * sxx - sx * sx;
  return d > 0 ? (n * sxy - sx * sy) / d : 0;
}
}  // namespace

void ClockFollower::reset() {
  times_.clear();
  armed_ = running_ = false;
  pulses_ = 0;
}

ClockFollower::Action ClockFollower::feed(const MidiMessage& m) {
  Action a;
  switch (m.status) {
    case 0xFA:  // start
    case 0xFB:  // continue
      armed_ = true;
      from_start_ = m.status == 0xFA;
      break;
    case 0xFC:  // stop
      armed_ = false;
      if (running_) a.kind = Action::Stop;
      running_ = false;
      break;
    case 0xF2:  // song position, in sixteenths
      if (!running_) {
        a.kind = Action::Locate;
        a.position = Tick((m.data1 & 0x7F) | ((m.data2 & 0x7F) << 7)) * (kTicksPerQuarter / 4);
      }
      break;
    case 0xF8: {  // clock
      if (!times_.empty() && m.time_us - times_.back() > kRestartUs) times_.clear();
      times_.push_back(m.time_us);
      if (times_.size() > kWindow) times_.erase(times_.begin());
      estimate();
      last_us_ = m.time_us;
      if (armed_) {
        armed_ = false;
        running_ = true;
        pulses_ = 0;
        a.kind = Action::Play;
        a.from_start = from_start_;
      } else if (running_) {
        ++pulses_;
      }
      break;
    }
    default:
      break;
  }
  return a;
}

void ClockFollower::estimate() {
  if (times_.size() < 8) return;
  double per = slope(times_, 0);
  // A tempo change: the last beat disagrees with the whole window, so start again from it.
  if (times_.size() >= kShort * 2) {
    const double recent = slope(times_, times_.size() - kShort);
    if (std::fabs(recent - per) > 0.02 * per) {
      times_.erase(times_.begin(), times_.end() - long(kShort));
      per = recent;
    }
  }
  if (per <= 0) return;
  interval_us_ = per;
  bpm_ = std::max(20.0, std::min(400.0, 60e6 / (24 * per)));
}

double ClockFollower::beats_at(int64_t now_us) const {
  if (!running_) return 0;
  const double since = std::min(1.0, std::max(0.0, double(now_us - last_us_) / interval_us_));
  return (double(pulses_) + since) / 24.0;
}

}  // namespace twill
