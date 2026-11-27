#include "platform/sdl2/AlsaCapture.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <type_traits>

#ifdef __linux__
#include <dlfcn.h>
#include <pthread.h>
#include <sched.h>
#endif

namespace twill {

void AlsaCapture::to_float(const uint8_t* p, size_t n, int bytes, bool low24, float* out) {
  for (size_t i = 0; i < n; ++i, p += bytes) {
    uint32_t u;  // the sample in the top bits of 32
    if (bytes == 2) u = uint32_t(p[0]) << 16 | uint32_t(p[1]) << 24;
    else if (bytes == 3) u = uint32_t(p[0]) << 8 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 24;
    else if (low24) u = uint32_t(p[0]) << 8 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 24;
    else u = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    out[i] = float(int32_t(u)) * (1.0f / 2147483648.0f);
  }
}

#ifdef __linux__
namespace {

// The few alsa-lib calls used, looked up once. Types are kept opaque (void*): their values are
// alsa-lib's, from <alsa/pcm.h>.
constexpr int kStreamCapture = 1;
constexpr int kAccessRwInterleaved = 3;
constexpr int kFormatS16 = 2, kFormatS24 = 6, kFormatS32 = 10, kFormatS24_3 = 32;  // little-endian

struct Alsa {
  bool loaded = false;
  int (*pcm_open)(void**, const char*, int, int) = nullptr;
  int (*pcm_close)(void*) = nullptr;
  int (*hw_malloc)(void**) = nullptr;
  void (*hw_free)(void*) = nullptr;
  int (*hw_any)(void*, void*) = nullptr;
  int (*hw_set_access)(void*, void*, int) = nullptr;
  int (*hw_test_format)(void*, void*, int) = nullptr;
  int (*hw_set_format)(void*, void*, int) = nullptr;
  int (*hw_test_channels)(void*, void*, unsigned) = nullptr;
  int (*hw_set_channels)(void*, void*, unsigned) = nullptr;
  int (*hw_set_rate_near)(void*, void*, unsigned*, int*) = nullptr;
  int (*hw_set_period_near)(void*, void*, unsigned long*, int*) = nullptr;
  int (*hw_set_buffer_near)(void*, void*, unsigned long*) = nullptr;
  int (*hw_params)(void*, void*) = nullptr;
  long (*readi)(void*, void*, unsigned long) = nullptr;
  int (*recover)(void*, int, int) = nullptr;
  int (*start)(void*) = nullptr;
  const char* (*strerror)(int) = nullptr;
};

const Alsa& alsa() {
  static Alsa a = [] {
    Alsa x;
    void* h = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!h) return x;
    auto get = [h](auto& fn, const char* name) {
      fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(h, name));
      return fn != nullptr;
    };
    x.loaded = get(x.pcm_open, "snd_pcm_open") && get(x.pcm_close, "snd_pcm_close") &&
               get(x.hw_malloc, "snd_pcm_hw_params_malloc") && get(x.hw_free, "snd_pcm_hw_params_free") &&
               get(x.hw_any, "snd_pcm_hw_params_any") && get(x.hw_set_access, "snd_pcm_hw_params_set_access") &&
               get(x.hw_test_format, "snd_pcm_hw_params_test_format") &&
               get(x.hw_set_format, "snd_pcm_hw_params_set_format") &&
               get(x.hw_test_channels, "snd_pcm_hw_params_test_channels") &&
               get(x.hw_set_channels, "snd_pcm_hw_params_set_channels") &&
               get(x.hw_set_rate_near, "snd_pcm_hw_params_set_rate_near") &&
               get(x.hw_set_period_near, "snd_pcm_hw_params_set_period_size_near") &&
               get(x.hw_set_buffer_near, "snd_pcm_hw_params_set_buffer_size_near") &&
               get(x.hw_params, "snd_pcm_hw_params") && get(x.readi, "snd_pcm_readi") &&
               get(x.recover, "snd_pcm_recover") && get(x.start, "snd_pcm_start") &&
               get(x.strerror, "snd_strerror");
    return x;
  }();
  return a;
}

std::string trimmed(const char* s) {
  std::string out = s;
  while (!out.empty() && (out.back() == '\n' || out.back() == ' ' || out.back() == '\r')) out.pop_back();
  return out;
}

}  // namespace

std::string AlsaCapture::device_for(const std::string& listed, bool plug) {
  if (listed.empty()) return std::string();
  FILE* f = std::fopen("/proc/asound/cards", "r");
  if (!f) return std::string();
  char line[512];
  std::string found;
  while (found.empty() && std::fgets(line, sizeof line, f)) {
    // " 2 [Felucca        ]: USB-Audio - Felucca"
    int num = -1;
    char id[64] = {0};
    if (std::sscanf(line, " %d [%63[^] ]", &num, id) != 2) continue;
    const char* dash = std::strstr(line, " - ");
    if (!dash) continue;
    const std::string card_name = trimmed(dash + 3);
    for (int dev = 0; dev < 32 && found.empty(); ++dev) {
      char path[96];
      std::snprintf(path, sizeof path, "/proc/asound/card%d/pcm%dc/info", num, dev);
      FILE* info = std::fopen(path, "r");
      if (!info) continue;
      char l2[256];
      while (std::fgets(l2, sizeof l2, info)) {
        if (std::strncmp(l2, "name: ", 6) != 0) continue;
        if (card_name + ", " + trimmed(l2 + 6) == listed)
          found = std::string(plug ? "plughw" : "hw") + ":CARD=" + id + ",DEV=" + std::to_string(dev);
        break;
      }
      std::fclose(info);
    }
  }
  std::fclose(f);
  return found;
}

bool AlsaCapture::open(const std::string& pcm, int want_rate, std::string& error) {
  close();
  const Alsa& a = alsa();
  if (!a.loaded) {
    error = "alsa-lib isn't there";
    return false;
  }
  void* h = nullptr;
  int err = a.pcm_open(&h, pcm.c_str(), kStreamCapture, 0);
  if (err < 0) {
    error = a.strerror(err);
    return false;
  }
  void* hw = nullptr;
  a.hw_malloc(&hw);
  auto fail = [&](const char* what, int e) {
    error = std::string(what) + ": " + a.strerror(e);
    a.hw_free(hw);
    a.pcm_close(h);
    return false;
  };
  if ((err = a.hw_any(h, hw)) < 0) return fail("no settings", err);
  if ((err = a.hw_set_access(h, hw, kAccessRwInterleaved)) < 0) return fail("access", err);
  struct {
    int id, bytes;
    const char* name;
  } const formats[] = {{kFormatS16, 2, "S16_LE"}, {kFormatS32, 4, "S32_LE"}, {kFormatS24, 4, "S24_LE"}, {kFormatS24_3, 3, "S24_3LE"}};
  int chosen = -1;
  for (int i = 0; i < 4 && chosen < 0; ++i)
    if (a.hw_test_format(h, hw, formats[i].id) == 0) chosen = i;
  if (chosen < 0) return fail("format", -22);
  if ((err = a.hw_set_format(h, hw, formats[chosen].id)) < 0) return fail("format", err);
  unsigned ch = a.hw_test_channels(h, hw, 2) == 0 ? 2 : 1;
  if ((err = a.hw_set_channels(h, hw, ch)) < 0) return fail("channels", err);
  unsigned rate = unsigned(want_rate > 0 ? want_rate : 48000);
  if ((err = a.hw_set_rate_near(h, hw, &rate, nullptr)) < 0) return fail("rate", err);
  // Short periods for monitoring, a long buffer (16 of them, about 85 ms) so a late read loses nothing.
  unsigned long period = 256, buffer = 256 * 16;
  if ((err = a.hw_set_period_near(h, hw, &period, nullptr)) < 0) return fail("period", err);
  if ((err = a.hw_set_buffer_near(h, hw, &buffer)) < 0) return fail("buffer", err);
  if ((err = a.hw_params(h, hw)) < 0) return fail("settings", err);
  a.hw_free(hw);

  pcm_ = h;
  rate_ = int(rate);
  channels_ = int(ch);
  period_ = int(period);
  sample_bytes_ = formats[chosen].bytes;
  format_id_ = formats[chosen].id;
  format_ = formats[chosen].name;
  raw_.assign(size_t(period_) * size_t(channels_) * size_t(sample_bytes_), 0);
  conv_.assign(size_t(period_) * size_t(channels_), 0.0f);
  overruns_ = 0;
  return true;
}

void AlsaCapture::start(Block block, void* user) {
  if (!pcm_ || thread_.joinable()) return;
  block_ = block;
  user_ = user;
  stop_ = false;
  thread_ = std::thread([this] { run(); });
}

void AlsaCapture::run() {
  // The same priority as the output's (Sdl2Platform), below the sound system's own threads.
  sched_param sp{};
  sp.sched_priority = 70;
  pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
  const Alsa& a = alsa();
  a.start(pcm_);
  const size_t values = size_t(period_) * size_t(channels_);
  while (!stop_.load(std::memory_order_relaxed)) {
    const long got = a.readi(pcm_, raw_.data(), (unsigned long)period_);
    if (got < 0) {
      if (got == -32) overruns_.fetch_add(1, std::memory_order_relaxed);  // -EPIPE
      if (a.recover(pcm_, int(got), 1) < 0) break;
      a.start(pcm_);
      continue;
    }
    const size_t n = size_t(got) * size_t(channels_);
    to_float(raw_.data(), std::min(n, values), sample_bytes_, format_id_ == kFormatS24, conv_.data());
    if (block_ && n) block_(user_, conv_.data(), n);
  }
}

void AlsaCapture::close() {
  stop_ = true;
  // The read wakes within a period (or the device's going away ends it).
  if (thread_.joinable()) thread_.join();
  if (pcm_) alsa().pcm_close(pcm_);
  pcm_ = nullptr;
}

#else  // not Linux

std::string AlsaCapture::device_for(const std::string&, bool) { return std::string(); }
bool AlsaCapture::open(const std::string&, int, std::string& error) {
  error = "not Linux";
  return false;
}
void AlsaCapture::start(Block, void*) {}
void AlsaCapture::run() {}
void AlsaCapture::close() {}

#endif

}  // namespace twill
