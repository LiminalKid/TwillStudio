// Linux: the kernel's raw MIDI devices (/dev/snd/midiC<card>D<device>), read and written
// directly, so no ALSA library is needed. USB MIDI devices appear there with the class-compliant
// driver. Devices plugged in later are found by rescanning every two seconds.
//
// A thread of its own waits on the devices and stamps each message as it arrives (following an
// external clock needs to know when each pulse came, closer than a UI frame); poll() hands over
// what it has collected.
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "platform/sdl2/SystemMidi.h"

namespace twill {

namespace {

int64_t now_us() {
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

class RawMidi : public MidiPort {
 public:
  RawMidi() : reader_([this] { run(); }) {}
  ~RawMidi() override {
    quit_.store(true);
    reader_.join();
    for (Device& d : devices_) close(d.fd);
  }

  void poll(std::vector<MidiMessage>& in) override {
    std::lock_guard<std::mutex> lock(mutex_);
    in.insert(in.end(), pending_.begin(), pending_.end());
    pending_.clear();
  }

  void send(const MidiMessage& m) override { send_except(m, -1); }

  // From the MIDI sender's thread and the UI's; the device list is locked.
  void send_except(const MidiMessage& m, int32_t source) override {
    std::lock_guard<std::mutex> lock(mutex_);
    const uint8_t bytes[3] = {m.status, m.data1, m.data2};
    for (Device& d : devices_)
      if (d.writable && d.id != source) (void)!write(d.fd, bytes, size_t(m.size()));
  }

  std::string describe() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (devices_.empty()) return "no devices";
    return std::to_string(devices_.size()) + (devices_.size() == 1 ? " device" : " devices");
  }

 private:
  struct Device {
    std::string path;
    int32_t id;  // the device's MidiMessage::source; one raw MIDI device is one input and output
    int fd;
    bool writable;
    MidiParser parser;
  };

  // The reader thread. Only it opens and closes devices (and the destructor, after it ends), so
  // the descriptors it waits on stay valid while it waits without the lock.
  void run() {
    std::vector<pollfd> fds;
    std::vector<MidiMessage> got;
    uint8_t buf[256];
    while (!quit_.load()) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        rescan_if_due();
        fds.clear();
        for (const Device& d : devices_) fds.push_back(pollfd{d.fd, POLLIN, 0});
      }
      if (fds.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }
      if (::poll(fds.data(), nfds_t(fds.size()), 50) <= 0) continue;
      const int64_t now = now_us();
      std::lock_guard<std::mutex> lock(mutex_);
      for (size_t i = 0, k = 0; i < devices_.size() && k < fds.size(); ++k) {
        Device& d = devices_[i];
        if (!fds[k].revents) {
          ++i;
          continue;
        }
        ssize_t n;
        got.clear();
        while ((n = read(d.fd, buf, sizeof buf)) > 0) d.parser.feed(buf, size_t(n), got);
        for (MidiMessage& m : got) {
          m.source = d.id;
          m.time_us = now;
        }
        pending_.insert(pending_.end(), got.begin(), got.end());
        const bool gone = n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) ||
                          (fds[k].revents & (POLLERR | POLLHUP | POLLNVAL));
        if (gone) {  // unplugged
          close(d.fd);
          devices_.erase(devices_.begin() + long(i));
        } else {
          ++i;
        }
      }
      if (pending_.size() > 4096) pending_.erase(pending_.begin(), pending_.end() - 4096);  // nobody polling
    }
  }

  void rescan_if_due() {
    const auto now = std::chrono::steady_clock::now();
    if (now < next_scan_) return;
    next_scan_ = now + std::chrono::seconds(2);
    char path[64];
    for (int card = 0; card < 8; ++card)
      for (int dev = 0; dev < 4; ++dev) {
        std::snprintf(path, sizeof path, "/dev/snd/midiC%dD%d", card, dev);
        bool open_already = false;
        for (const Device& d : devices_) open_already |= d.path == path;
        if (open_already || access(path, F_OK) != 0) continue;
        int fd = open(path, O_RDWR | O_NONBLOCK);
        bool writable = fd >= 0;
        if (fd < 0) fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) devices_.push_back(Device{path, next_id_++, fd, writable, MidiParser()});
      }
  }

  mutable std::mutex mutex_;
  std::vector<Device> devices_;
  std::vector<MidiMessage> pending_;
  int32_t next_id_ = 0;
  std::chrono::steady_clock::time_point next_scan_{};
  std::atomic<bool> quit_{false};
  std::thread reader_;  // last: it starts once everything above is ready
};

}  // namespace

std::unique_ptr<MidiPort> make_system_midi() { return std::make_unique<RawMidi>(); }

}  // namespace twill
