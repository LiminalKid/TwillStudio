// A platform with no display, sound or controller: for the benchmark and the scripted UI tests.
// Files live in memory; input comes from set_held().
#pragma once
#include <chrono>
#include <map>
#include <string>

#include "platform/Platform.h"

namespace twill {

class NullPlatform : public Platform {
 public:
  NullPlatform(int w = 1024, int h = 768, float scale = 2) : size_{w, h}, scale_(scale) {}

  void set_held(uint32_t buttons) { held_ = buttons; }
  // Time stands still unless advanced, so key repeat in tests is deterministic.
  void advance_ms(uint64_t ms) { now_us_ += ms * 1000; }
  std::map<std::string, std::vector<uint8_t>> files;

  const char* name() const override { return "null"; }
  bool audio_open(int, int, AudioCallback, void*) override { return true; }
  AudioInfo audio_info() const override { return {48000, 256}; }
  void audio_close() override {}
  Size display_size() const override { return size_; }
  float ui_scale() const override { return scale_; }
  void present(const uint32_t*, int, const std::vector<Rect>&) override {}
  void poll_input(InputState& out) override { out.held = held_; }
  const char* controller_name() const override { return "none"; }
  // Paths inside the data folder ("/null/...") are stored relative to it, like real ones.
  static std::string rel(const char* path) {
    std::string p = path;
    return p.compare(0, 6, "/null/") == 0 ? p.substr(6) : p;
  }
  bool read_file(const char* name, std::vector<uint8_t>& out) override {
    auto it = files.find(rel(name));
    if (it == files.end()) return false;
    out = it->second;
    return true;
  }
  bool write_file(const char* name, const std::vector<uint8_t>& data) override {
    files[rel(name)] = data;
    return true;
  }
  bool list_dir(const char* path, std::vector<DirEntry>& out) override {
    // Files whose names start with path + '/': their next component, as a file or a folder.
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
