// Allwinner A133 (sun50iw10) codec, as in the TrimUI Brick Pro: the device tree's `dacdrc_cfg`
// switches on a dynamic range compressor in the DAC's processing block. It is bit 29 (DRC enable)
// of DAC_DAP_CTL, at offset 0xF0 from the codec's registers. Only that bit is touched; a reboot
// would restore it too. Needs root (/dev/mem), which these handhelds run apps as.
#include "platform/sdl2/OutputCompressor.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>

namespace twill {

namespace {

constexpr const char* kCodecNode = "/sys/firmware/devicetree/base/soc@03000000/codec@0x05096000";
constexpr off_t kCodecBase = 0x05096000;
constexpr unsigned kDapCtl = 0xF0;
constexpr uint32_t kDrcEnable = 1u << 29;

// The device tree's 32-bit big-endian value for `name`, or -1.
long dt_value(const char* name) {
  char path[160];
  std::snprintf(path, sizeof path, "%s/%s", kCodecNode, name);
  FILE* f = std::fopen(path, "rb");
  if (!f) return -1;
  unsigned char b[4];
  const bool ok = std::fread(b, 1, 4, f) == 4;
  std::fclose(f);
  return ok ? long(uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3]) : -1;
}

// Runs `f` on the DAC_DAP_CTL register. False if the registers can't be reached.
template <typename F>
bool with_dap_ctl(F&& f) {
  const int fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (fd < 0) return false;
  void* page = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, kCodecBase);
  close(fd);
  if (page == MAP_FAILED) return false;
  f(*reinterpret_cast<volatile uint32_t*>(static_cast<uint8_t*>(page) + kDapCtl));
  munmap(page, 4096);
  return true;
}

}  // namespace

std::string OutputCompressor::disable() {
  if (changed_ || dt_value("dacdrc_cfg") <= 0) return "";  // not this chip, or no compressor
  uint32_t was = 0;
  if (!with_dap_ctl([&](volatile uint32_t& reg) {
        was = reg;
        if (was & kDrcEnable) reg = was & ~kDrcEnable;
      }))
    return "output compressor: can't reach the codec's registers";
  if (!(was & kDrcEnable)) return "output compressor: already off";
  changed_ = true;
  original_ = was;
  return "output compressor: turned off while running";
}

void OutputCompressor::restore() {
  if (!changed_) return;
  changed_ = false;
  with_dap_ctl([&](volatile uint32_t& reg) { reg = reg | (original_ & kDrcEnable); });
}

}  // namespace twill
