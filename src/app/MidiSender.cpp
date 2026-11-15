#include "app/MidiSender.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace twill {

int64_t MidiSender::now_us() {
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void MidiSender::pump(int64_t now) {
  constexpr double kUsPerSample = 1e6 / Engine::kSampleRate;
  Engine::MidiEvent e;
  while (engine_.pop_midi_out(e)) {
    switch (e.kind) {
      case Engine::MidiEvent::Anchor: {
        // Callbacks run early or late by up to a block; follow the average slowly, but jump if
        // the two drift far apart (the audio stalled, or this is the first anchor).
        const double offset = double(e.wall_us) - double(e.sample) * kUsPerSample;
        if (!have_offset_ || std::fabs(offset - offset_us_) > 50000) offset_us_ = offset;
        else offset_us_ += (offset - offset_us_) * 0.05;
        have_offset_ = true;
        break;
      }
      case Engine::MidiEvent::Now:
        if (port_) port_->send(e.m);
        break;
      case Engine::MidiEvent::Message:
        pending_.push_back(Pending{e.sample, e.m});
        break;
    }
  }
  if (pending_.empty()) return;
  // Events come in order within a callback; keep them in order across callbacks too.
  std::stable_sort(pending_.begin(), pending_.end(), [](const Pending& a, const Pending& b) { return a.sample < b.sample; });
  const double latency = double(latency_us_.load());
  size_t sent = 0;
  for (; sent < pending_.size(); ++sent) {
    const double due = offset_us_ + double(pending_[sent].sample) * kUsPerSample + latency;
    if (have_offset_ && due > double(now)) break;
    if (port_) port_->send(pending_[sent].m);
  }
  pending_.erase(pending_.begin(), pending_.begin() + long(sent));
}

void MidiSender::start_thread() {
  if (thread_.joinable()) return;
  quit_.store(false);
  thread_ = std::thread([this] {
    while (!quit_.load()) {
      pump(now_us());
      std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
  });
}

void MidiSender::stop() {
  if (!thread_.joinable()) return;
  quit_.store(true);
  thread_.join();
}

}  // namespace twill
