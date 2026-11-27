// Recording straight from an ALSA card on a Linux handheld, without SDL: the card's own hw device,
// in a format it gives natively (16, 24 or 32-bit), read in short periods from a long buffer.
//
// Through SDL, a USB synth's input on the TrimUI Brick (stock firmware, and sndstation before it
// used the SDK's own input) lost bits of the sound now and then, clicks in every take: SDL opens a
// card through ALSA's dsnoop or plughw, whose conversions overran. Read this way (as arecord, and
// sndstation's InputMeter, do) it doesn't.
//
// alsa-lib is loaded when first needed (dlopen), so nothing new is linked and a system without it
// just falls back to SDL. Linux only; elsewhere open() says no.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace twill {

class AlsaCapture {
 public:
  // Each period read, on the reading thread: interleaved float (-1..1), `n` values. Must not block.
  using Block = void (*)(void* user, const float* data, size_t n);

  ~AlsaCapture() { close(); }

  // Opens `pcm` ("hw:CARD=Felucca,DEV=0") for capture, near `want_rate`, with its first two
  // channels or one, in the first format it takes of S16, S32, S24 in 4 bytes, S24 in 3. Doesn't
  // start reading. False with `error` set if it can't.
  bool open(const std::string& pcm, int want_rate, std::string& error);
  // Starts the reading thread (real-time priority where allowed, as the output's).
  void start(Block block, void* user);
  void close();

  bool is_open() const { return pcm_ != nullptr; }
  int rate() const { return rate_; }
  int channels() const { return channels_; }
  const char* format() const { return format_; }
  int period() const { return period_; }
  // Times the card overran (the thread was late and sound was lost) since open().
  int overruns() const { return overruns_.load(std::memory_order_relaxed); }

  // The hw device for a listed input ("Felucca, USB Audio": "<card name>, <device name>", as SDL
  // and alsa-lib name it), from /proc/asound; empty if there is none. `plug` gives plughw instead.
  static std::string device_for(const std::string& listed, bool plug = false);

  // Little-endian samples to float: `bytes` 2 (S16), 3 (S24 packed), 4 (S32, or with `low24` S24
  // in 4 bytes, its top byte unused). For the tests too.
  static void to_float(const uint8_t* in, size_t n, int bytes, bool low24, float* out);

 private:
  void run();

  void* pcm_ = nullptr;  // snd_pcm_t*
  int rate_ = 0, channels_ = 0, period_ = 0;
  const char* format_ = "";
  std::atomic<int> overruns_{0};
#ifdef __linux__
  int sample_bytes_ = 2, format_id_ = 0;
  std::vector<uint8_t> raw_;
  std::vector<float> conv_;
  Block block_ = nullptr;
  void* user_ = nullptr;
  std::atomic<bool> stop_{false};
  std::thread thread_;
#endif
};

}  // namespace twill
