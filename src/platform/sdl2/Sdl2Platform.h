// SDL2 platform: macOS, Linux and Windows desktops, and Linux handhelds such as the TrimUI Brick Pro.
#pragma once
#include <SDL.h>

#include <cstdio>

#include <string>

#include <atomic>
#include <memory>

#include "platform/Platform.h"
#include "platform/sdl2/OutputCompressor.h"

namespace twill {

// Raw joystick button numbers for one device. -1 means "not present"; the d-pad uses the hat
// unless it has its own buttons.
struct PadMap {
  int a = -1, b = -1, x = -1, y = -1, l = -1, r = -1, l2 = -1, r2 = -1, select = -1, start = -1,
      menu = -1;
  int up = -1, down = -1, left = -1, right = -1;
};

class Sdl2Platform : public Platform {
 public:
  Sdl2Platform();
  ~Sdl2Platform() override;
  bool init();  // false if no window could be created

  const char* name() const override { return name_; }

  bool audio_open(int preferred_rate, int preferred_block, AudioCallback cb, void* user) override;
  AudioInfo audio_info() const override { return audio_info_; }
  void audio_close() override;
  bool capture_available() override;
  std::vector<std::string> capture_devices() override;
  bool capture_open(const std::string& device, CaptureInfo& info, std::string& error) override;
  size_t capture_read(std::vector<float>& out) override;
  void capture_close() override;
  void capture_monitor(bool on, int channel, float gain) override;

  Size display_size() const override { return size_; }
  float ui_scale() const override { return ui_scale_; }
  void present(const uint32_t* pixels, int stride, const std::vector<Rect>& dirty) override;

  void poll_input(InputState& out) override;
  const char* controller_name() const override;

  // From M1's benchmarks (Docs/DESIGN.md §6): on the Brick Pro the heaviest macro-oscillator engine
  // costs 2.7% of a core per voice, so 16 synth voices stay under half of one core. Playing a
  // sample costs far less, so samples get 16 voices more (multisample chords with release tails
  // and layered zones need them).
  int max_tracks() const override { return 8; }
  int max_voices() const override { return handheld_ ? 32 : 64; }
  int max_synth_voices() const override { return handheld_ ? 16 : 0; }

  bool read_file(const char* path, std::vector<uint8_t>& out) override;
  bool write_file(const char* path, const std::vector<uint8_t>& data) override;
  bool list_dir(const char* path, std::vector<DirEntry>& out) override;
  bool make_dir(const char* path) override;
  bool remove_file(const char* path) override { return std::remove(resolve(path).c_str()) == 0; }
  std::string data_dir() const override { return data_dir_; }
  MidiPort* midi() override { return midi_.get(); }
  void keep_awake(bool on) override;
  bool prefers_long_audio_buffer() const override;
  bool can_quit() const override;
  bool select_start_quits() const override;
  // Where the system sleeps without stopping programs (TWILL_SLEEP_FILE, muOS): the sound and the
  // app stop until it wakes, or asks the app to quit. Called between frames.
  void wait_while_asleep();
  // On a handheld, from the memory free when the app starts (design §6, "Multisamples"); no limit
  // on a computer.
  size_t sample_budget() const override { return sample_budget_; }

  uint64_t micros() const override;
  void log(const char* fmt, ...) override;
  // The budget for `available` bytes of free memory: 60% of what's left after a reserve for the
  // system and the app itself, from 64 MB to 1 GB.
  static size_t budget_for(size_t available);

 protected:
  // Hooks for a platform built on this one (SndstationPlatform): the system's MIDI devices; the
  // game controllers, opened at start; an event, before the default handling (true: handled); and
  // the buttons held, read once per poll after the events.
  virtual std::unique_ptr<MidiPort> make_midi();
  virtual void open_input();
  virtual bool take_event(const SDL_Event&) { return false; }
  virtual uint32_t read_buttons() { return keys_ | read_pad(); }
  // Monitoring: adds what the input has brought in to an output block (stereo), from the thread
  // that makes the output. SDL's own output calls it; a platform with an output of its own must too.
  void mix_monitor(float* out, int frames);
  // Recording, for a platform with an input of its own (SndstationPlatform): the ring and the
  // monitor set up for an input at `rate` with `channels` (before it starts), what it brings
  // (from its thread: interleaved float, `n` values), and its end. capture_read and
  // capture_monitor then work as for SDL's input.
  void capture_begin(int rate, int channels);
  void take_capture(const float* in, size_t n);
  void capture_end();
  bool capture_allowed() const;
  int capture_channels() const { return capture_info_.channels; }

  const char* name_ = "SDL2";
  std::string data_dir_;  // ends with a slash
  bool handheld_ = false;
  size_t sample_budget_ = 0;

 private:
  static void sdl_audio_callback(void* user, Uint8* stream, int len);
  static void sdl_capture_callback(void* user, Uint8* stream, int len);
  std::string alsa_plughw_for(const std::string& listed) const;
  SDL_AudioDeviceID open_capture_device(const std::string& device, const SDL_AudioSpec& want, SDL_AudioSpec& have,
                                        int allow, std::string* used) const;
  void open_controller(int device_index);
  void follow_page_size();
  void update_size();
  bool pad_map_from_frontend(const char* joystick_name);
  uint32_t read_pad() const;
  // A mouse or a finger's event into pointer_, in display pixels (not on handhelds).
  void take_pointer(const SDL_Event& e);
  std::string resolve(const char* path) const { return path[0] == '/' ? path : data_dir_ + path; }

  bool awake_held_ = false;  // keep_awake's files are in place
#ifdef __linux__
  // Audio thread only: real-time scheduling, and the frames in a row that were late or in time.
  bool audio_rt_tried_ = false, audio_rt_ = false;
  int audio_late_run_ = 0, audio_ok_run_ = 0;
#endif
  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* texture_ = nullptr;
  Size size_;      // what the app draws: landscape, even on a portrait panel
  int rotate_ = 0;  // degrees the picture is turned on its way to a portrait panel (90 or 270)
  float ui_scale_ = 1;

  SDL_AudioDeviceID audio_dev_ = 0;
  OutputCompressor compressor_;  // handhelds: the chip's own, off while audio is open
  AudioInfo audio_info_;
  AudioCallback audio_cb_ = nullptr;
  void* audio_user_ = nullptr;

  // Recording: the input's audio thread writes into the ring, the app's thread reads from it
  // (one writer, one reader); what doesn't fit is dropped.
  SDL_AudioDeviceID capture_dev_ = 0;
  CaptureInfo capture_info_;
  std::vector<float> capture_ring_;
  std::atomic<size_t> capture_write_{0}, capture_read_{0};
  std::string capture_name_;  // the input open now ("" for the default)
  std::atomic<bool> capture_live_{false};
  int presented_ = 0;  // frames shown, for the log (the first two)
  std::unique_ptr<class AlsaCapture> alsa_in_;  // handhelds: an input read from the card itself  // an input is open (SDL's or a subclass's)
  std::atomic<int> monitor_in_rate_{48000};  // the input's rate, for monitoring at the output's
  bool capture_s16_ = false;          // the input comes as 16-bit (handhelds), converted here
  std::vector<float> capture_conv_;   // the input's thread's: a 16-bit block as float
  // Monitoring: the input thread writes the chosen channel, gain applied, into this ring; the
  // output thread mixes it in. The settings are the app's, read by the input thread.
  std::vector<float> monitor_ring_;
  std::atomic<size_t> monitor_write_{0}, monitor_read_{0};
  std::atomic<bool> monitor_on_{false};
  std::atomic<int> monitor_channel_{0};
  std::atomic<float> monitor_gain_{1.0f};
  // The output thread's own: whether it has enough in hand yet, where it reads between samples,
  // and how full the ring has been of late.
  bool monitor_primed_ = false;
  double monitor_pos_ = 0, monitor_fill_ = 0;
  // What happened to it, for the log: the largest burst the input has brought at once (frames),
  // the times the output ran dry (a gap) or fell far behind (a skip).
  std::atomic<int> monitor_burst_{0}, monitor_dry_{0}, monitor_skips_{0};
  int monitor_logged_[3] = {0, 0, 0};
  uint64_t monitor_logged_us_ = 0;
  [[maybe_unused]] bool capture_rt_tried_ = false;  // the input's thread asked for real-time scheduling (Linux handhelds)
  // Handhelds: the inputs SDL lists, and of those, the ones that opened when tried.
  std::vector<std::string> capture_listed_, capture_usable_;

  SDL_Joystick* joystick_ = nullptr;
  SDL_GameController* controller_ = nullptr;
  PadMap pad_map_;
  bool has_pad_map_ = false;
  int raw_presses_logged_ = 0;  // the first few raw button numbers go to the log
  uint32_t keys_ = 0;
  bool quit_ = false;
  std::vector<PointerEvent> pointer_;  // since the last poll
#ifndef TWILL_HANDHELD
  uint32_t last_touch_ms_ = 0;         // a browser follows a tap with a mouse click of its own
#endif
  std::unique_ptr<MidiPort> midi_;
};

}  // namespace twill
