// sndstation platform: Linux handhelds running sndstation (Debian with sndstation's launcher and
// SDK; see BUILDING.md). The screen is the SDL2 platform's; sound, buttons and MIDI are the SDK's,
// so Twill follows the unit's own setup: the card, buffer size and volume set in the launcher's
// Audio page, every pad the SDK knows (by position: A is the right-hand face button), and the MIDI
// ports chosen in its MIDI page.
#pragma once
#include <sndstation/audiocard.h>
#include <sndstation/audioin.h>
#include <sndstation/audioout.h>
#include <sndstation/pad.h>

#include <atomic>

#include "platform/sdl2/Sdl2Platform.h"

namespace twill {

class SndstationPlatform : public Sdl2Platform {
 public:
  SndstationPlatform();
  ~SndstationPlatform() override;
  bool init();

  bool audio_open(int preferred_rate, int preferred_block, AudioCallback cb, void* user) override;
  AudioInfo audio_info() const override { return audio_info_; }
  void audio_close() override;
  bool system_sets_audio_buffer() const override { return true; }
  // Menu (FN, or Select then Start) is for quitting and sleeping, as in the launcher.
  bool menu_quits() const override { return true; }
  bool sleep(std::string& why) override;
  bool prefers_long_audio_buffer() const override { return false; }

  const char* controller_name() const override;

  // Recording through the SDK's input (InputMeter: the card's own hw device, in its own format, on
  // alsa-lib), not SDL's: SDL asked plughw for converted samples, and on the Brick Pro that
  // overran and lost bits of a USB synth's sound. Inputs are the cards that record, by name.
  bool capture_available() override;
  std::vector<std::string> capture_devices() override;
  bool capture_open(const std::string& device, CaptureInfo& info, std::string& error) override;
  void capture_close() override;
  int capture_info_channels() const { return capture_channels(); }
#if TWILL_PLUGINS
  // CLAP instruments, through the SDK's host (SndstationPlugins.cpp).
  PluginHost* plugins() override;
#endif

 protected:
  std::unique_ptr<MidiPort> make_midi() override;
  void open_input() override {}  // the pad layer opens joysticks as SDL reports them
  bool take_event(const SDL_Event& e) override;
  uint32_t read_buttons() override;

 private:
  // The SDK hands both its callbacks one user pointer, this platform; render passes it on.
  static void render(void* self, float* out, int frames);
  static void rate_moved(void* self, int rate);

  sndstation::AudioOut out_;
  AudioInfo audio_info_;
  AudioCallback cb_ = nullptr;
  void* cb_user_ = nullptr;
  bool audio_open_ = false;
  sndstation::pad::State pad_;
  std::vector<sndstation::audiocard::Card> capture_cards();  // those that record, real ones
  static std::string input_name(const sndstation::audiocard::Card& card);
  sndstation::InputMeter in_;
  std::atomic<bool> in_ready_{false};  // its blocks go to the ring once the ring knows its shape
#if TWILL_PLUGINS
  std::unique_ptr<PluginHost> plugins_;
#endif
};

}  // namespace twill
