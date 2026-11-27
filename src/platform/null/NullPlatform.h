// A platform with no display, sound or controller: for the benchmark and the scripted UI tests.
// Files live in memory; input comes from set_held().
#pragma once
#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <string>

#include "platform/Platform.h"

namespace twill {

class NullPlatform : public Platform {
 public:
  NullPlatform(int w = 1024, int h = 768, float scale = 2) : size_{w, h}, scale_(scale) {}

  void set_held(uint32_t buttons) { held_ = buttons; }
  // As on sndstation: Menu opens the quit menu, and sleep() counts the sleeps.
  bool menu_is_for_quitting = false;
  bool quits_on_select_start = false;
  bool select_start_quits() const override { return quits_on_select_start; }
  int sleeps = 0;
  bool menu_quits() const override { return menu_is_for_quitting; }
  bool sleep(std::string& why) override {
    if (!menu_is_for_quitting) return Platform::sleep(why);
    ++sleeps;
    return true;
  }
  // Time stands still unless advanced, so key repeat in tests is deterministic.
  void advance_ms(uint64_t ms) { now_us_ += ms * 1000; }
  std::map<std::string, std::vector<uint8_t>> files;

  const char* name() const override { return "null"; }
  bool audio_open(int, int block, AudioCallback cb, void* user) override {
    opened_block = block;
    audio_cb = cb;
    audio_user = user;
    return !audio_fails;
  }
  AudioCallback audio_cb = nullptr;  // what audio_open was given, for a test to call as a device would
  void* audio_user = nullptr;
  int audio_rate = 48000;  // the rate the "device" runs at
  bool audio_fails = false;  // no output device, for tests
  int opened_block = 0;  // the block size last asked for

  // Recording, for tests: the inputs listed, and what the open one "hears" (interleaved, at
  // capture_info's rate and channels), handed over at the next capture_read.
  std::vector<std::string> inputs;
  std::string refuse = "(none)";  // an input that won't open
  CaptureInfo capture_info{48000, 1};
  std::vector<float> capture_feed;
  std::string capture_opened = "(closed)";  // the input open now ("" for the default), or "(closed)"
  bool default_input = false;  // the default records even with no inputs listed
  bool capture_available() override { return default_input || !inputs.empty(); }
  std::vector<std::string> capture_devices() override { return inputs; }
  bool capture_open(const std::string& device, CaptureInfo& info, std::string& error) override {
    if ((inputs.empty() && !default_input) || device == refuse) {
      error = "No input";
      return false;
    }
    capture_opened = device;
    info = capture_info;
    capture_feed.clear();
    return true;
  }
  size_t capture_read(std::vector<float>& out) override {
    if (capture_opened == "(closed)") return 0;
    out.insert(out.end(), capture_feed.begin(), capture_feed.end());
    const size_t frames = capture_feed.size() / size_t(capture_info.channels);
    capture_feed.clear();
    return frames;
  }
  void capture_close() override {
    capture_opened = "(closed)";
    monitor_on = false;
  }
  bool monitor_on = false;
  int monitor_channel = 0;
  float monitor_gain = 0;
  void capture_monitor(bool on, int channel, float gain) override {
    monitor_on = on && capture_opened != "(closed)";
    monitor_channel = channel;
    monitor_gain = gain;
  }
  AudioInfo audio_info() const override { return {audio_rate, 256}; }
  void audio_close() override {}
  Size display_size() const override { return size_; }
  float ui_scale() const override { return scale_; }
  void present(const uint32_t*, int, const std::vector<Rect>&) override {}
  void poll_input(InputState& out) override {
    out.held = held_;
    out.pointer.swap(pointer);
    pointer.clear();
  }
  std::vector<PointerEvent> pointer;  // handed over at the next poll
  const char* controller_name() const override { return "none"; }
  // Paths inside the data folder ("/null/...") are stored relative to it, like real ones.
  static std::string rel(const char* path) {
    std::string p = path;
    return p.compare(0, 6, "/null/") == 0 ? p.substr(6) : p;
  }
  // Locked: a preset loads on a thread of its own.
  mutable std::mutex files_lock;
  bool read_file(const char* name, std::vector<uint8_t>& out) override {
    std::lock_guard<std::mutex> hold(files_lock);
    auto it = files.find(rel(name));
    if (it == files.end()) return false;
    out = it->second;
    return true;
  }
  bool remove_file(const char* name) override {
    std::lock_guard<std::mutex> hold(files_lock);
    return files.erase(rel(name)) > 0;
  }
  bool write_file(const char* name, const std::vector<uint8_t>& data) override {
    std::lock_guard<std::mutex> hold(files_lock);
    files[rel(name)] = data;
    return true;
  }
  bool list_dir(const char* path, std::vector<DirEntry>& out) override {
    // Files whose names start with path + '/': their next component, as a file or a folder.
    std::lock_guard<std::mutex> hold(files_lock);
    out.clear();
    std::string prefix = rel(path);
    if (!prefix.empty() && prefix.back() != '/') prefix += '/';
    bool any = false;
    for (const auto& f : files) {
      if (f.first.compare(0, prefix.size(), prefix) != 0) continue;
      any = true;
      std::string rest = f.first.substr(prefix.size());
      size_t slash = rest.find('/');
      DirEntry e{slash == std::string::npos ? rest : rest.substr(0, slash), slash != std::string::npos};
      bool dup = false;
      for (const DirEntry& o : out) dup |= o.name == e.name;
      if (!dup) out.push_back(e);
    }
    // Folders first, then by name, as the real platforms list them.
    std::stable_sort(out.begin(), out.end(), [](const DirEntry& a, const DirEntry& b) {
      return a.is_dir != b.is_dir ? a.is_dir : a.name < b.name;
    });
    return any || dirs.count(rel(path)) || prefix.empty();
  }
  bool make_dir(const char* path) override {
    dirs[rel(path)] = true;
    return true;
  }
  std::string data_dir() const override { return "/null/"; }
  std::map<std::string, bool> dirs;
  // MIDI: tests put messages in `midi_in` and read what was sent from `midi_sent`.
  struct FakeMidi : MidiPort {
    std::vector<MidiMessage> in, sent;
    void poll(std::vector<MidiMessage>& out) override {
      out.insert(out.end(), in.begin(), in.end());
      in.clear();
    }
    std::vector<int32_t> sent_except;  // for each of `sent`: the device left out, or -1
    void send(const MidiMessage& m) override { send_except(m, -1); }
    void send_except(const MidiMessage& m, int32_t source) override {
      sent.push_back(m);
      sent_except.push_back(source);
    }
    std::string describe() const override { return "test"; }
  } fake_midi;
  MidiPort* midi() override { return &fake_midi; }
  bool real_time() const override { return false; }
  void keep_awake(bool on) override { awake = on; }
  bool awake = false;  // what the app last asked keep_awake for
  bool long_buffer = false;  // what prefers_long_audio_buffer answers
  bool prefers_long_audio_buffer() const override { return long_buffer; }
  size_t budget = 0;  // what sample_budget answers
  size_t sample_budget() const override { return budget; }
  int max_tracks() const override { return 8; }
  int max_voices() const override { return 16; }
  uint64_t micros() const override { return now_us_; }
  void log(const char*, ...) override {}

 private:
  Size size_;
  float scale_;
  uint32_t held_ = 0;
  uint64_t now_us_ = 1000000;
};

}  // namespace twill
