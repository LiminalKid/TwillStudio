#include "platform/sndstation/SndstationPlatform.h"

#include <sndstation/midi.h>
#include <sndstation/power.h>
#include <sndstation/rt.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace twill {

namespace sp = sndstation::pad;
namespace sm = sndstation::midi;

namespace {

[[maybe_unused]] int64_t steady_us() {
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// The unit's MIDI ports, as its MIDI settings choose them: every input chosen, now and as they are
// plugged in, and every output chosen. Messages cross the SDK as MIDI 2.0 packets (UMP); Twill
// speaks MIDI 1.0, which they carry without loss.
class SndstationMidi : public MidiPort, public sm::Sink {
 public:
  SndstationMidi() {
    tier_ = sm::start();
    open_outputs();
    sm::listen(this);
  }
  const char* tier_name() const { return sm::tierName(tier_); }
  ~SndstationMidi() override {
    sm::listen(nullptr);
    std::lock_guard<std::mutex> lock(mutex_);
    for (const sm::Port& p : outs_) sm::close(p);
    outs_.clear();
    sm::stop();
  }

  void poll(std::vector<MidiMessage>& in) override {
    std::lock_guard<std::mutex> lock(mutex_);
    in.insert(in.end(), pending_.begin(), pending_.end());
    pending_.clear();
  }

  void send(const MidiMessage& m) override { send_except(m, -1); }

  void send_except(const MidiMessage& m, int32_t source) override {
    // A channel message is a MIDI 1.0 channel voice packet (type 2); clock, start and stop are
    // system packets (type 1). Group 0 either way.
    const uint32_t body = uint32_t(m.status) << 16 | uint32_t(m.data1) << 8 | m.data2;
    const uint32_t ump = (m.status >= 0xF0 ? 0x10000000u : 0x20000000u) | body;
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string* skip = nullptr;
    if (source >= 0 && size_t(source) < source_names_.size()) skip = &source_names_[size_t(source)];
    for (const sm::Port& p : outs_)
      if (!skip || p.name != *skip) sm::send(p, &ump, 1);
  }

  std::string describe() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t ins = 0;
    for (const sm::Port& p : sm::ports()) ins += p.input;
    // Short, for a settings row on a small screen (the log names the MIDI tier).
    return std::to_string(ins) + " in, " + std::to_string(outs_.size()) + " out";
  }

  // The SDK's MIDI thread.
  void received(const sm::Port& from, const uint32_t* ump, size_t, uint64_t time_ns) override {
    const int32_t source = source_id(from.name);
#ifdef __linux__
    const int64_t t = int64_t(time_ns / 1000);  // CLOCK_MONOTONIC: the steady clock's, on Linux
#else
    (void)time_ns;
    const int64_t t = steady_us();
#endif
    to_bytes_.feed(ump, [&](const uint8_t* b, size_t n) {
      if (n == 0 || b[0] < 0x80 || b[0] == 0xF0) return;  // system exclusive isn't used
      MidiMessage m;
      m.status = b[0];
      m.data1 = n > 1 ? b[1] : 0;
      m.data2 = n > 2 ? b[2] : 0;
      m.source = source;
      m.time_us = t;
      std::lock_guard<std::mutex> lock(mutex_);
      pending_.push_back(m);
      if (pending_.size() > 4096) pending_.erase(pending_.begin(), pending_.end() - 4096);  // nobody polling
    });
  }

  void portsChanged() override { open_outputs(); }

 private:
  // One id per device name: a device's input and output share it, so send_except can leave out the
  // device a note came from.
  int32_t source_id(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < source_names_.size(); ++i)
      if (source_names_[i] == name) return int32_t(i);
    source_names_.push_back(name);
    return int32_t(source_names_.size() - 1);
  }

  void open_outputs() {
    std::vector<sm::Port> want = sm::outputs();
    std::lock_guard<std::mutex> lock(mutex_);
    for (const sm::Port& p : outs_) sm::close(p);
    outs_.clear();
    for (const sm::Port& p : want)
      if (sm::open(p)) outs_.push_back(p);
  }

  sm::Tier tier_ = sm::Tier::None;
  sm::UmpToBytes to_bytes_{16};  // MIDI thread only
  mutable std::mutex mutex_;
  std::vector<MidiMessage> pending_;
  std::vector<sm::Port> outs_;
  std::vector<std::string> source_names_;
};

}  // namespace

SndstationPlatform::SndstationPlatform() { name_ = "sndstation"; }

SndstationPlatform::~SndstationPlatform() { audio_close(); }

bool SndstationPlatform::init() {
  if (!Sdl2Platform::init()) return false;
  name_ = handheld_ ? "sndstation handheld" : "sndstation desktop";
  // Before the first joystick event: the pad layer maps it through the unit's calibration and the
  // SDK's gamepad database.
  sp::calLoad();
  // Songs, samples and renders in the home folder, where the launcher's file browser and Storage
  // page show them.
  if (handheld_) {
    const char* home = std::getenv("HOME");
    data_dir_ = std::string(home && *home ? home : ".") + "/TwillStudio/";
    if (!make_dir(data_dir_.c_str())) log("can't create %s", data_dir_.c_str());
  }
  log("data folder %s", data_dir_.c_str());
  return true;
}

std::unique_ptr<MidiPort> SndstationPlatform::make_midi() {
  auto m = std::make_unique<SndstationMidi>();
  log("MIDI tier: %s", m->tier_name());  // the settings row only counts ports
  return m;
}

// The engine runs at one rate, so the stream is opened at it; the buffer size is the unit's.
bool SndstationPlatform::audio_open(int rate, int, AudioCallback cb, void* user) {
  sndstation::AudioOut::Settings set = sndstation::AudioOut::loadSettings();
  if (set.rate != rate) log("audio: the unit's rate is %d (0: the device's own); asking for %d", set.rate, rate);
  set.rate = rate;
  sndstation::rt::applySettings(set.realtimePriority, set.cpu);
  cb_ = cb;
  cb_user_ = user;
  if (!out_.open(set, &SndstationPlatform::render, &SndstationPlatform::rate_moved, this) || !out_.deviceOpen()) {
    log("audio: no device would open");
    out_.close();
    return false;
  }
  out_.start();
  audio_open_ = true;
  audio_info_ = AudioInfo{out_.sampleRate(), out_.deviceFrames()};
  log("audio: %s on %s (card %s%s), %s, volume %s", out_.driverName().c_str(), out_.pcmName().c_str(),
      out_.cardId().empty() ? "chosen by the system" : out_.cardId().c_str(),
      out_.nativeStream() ? ", sndstation's own stream" : "", out_.endpoint().label.c_str(),
      sndstation::volume::text(out_.volumeDb()).c_str());
  return true;
}

void SndstationPlatform::audio_close() {
  if (!audio_open_) return;
  out_.close();
  audio_open_ = false;
}

// The audio thread.
void SndstationPlatform::render(void* self, float* out, int frames) {
  auto* p = static_cast<SndstationPlatform*>(self);
  p->cb_(p->cb_user_, out, frames);
  p->mix_monitor(out, frames);  // a sample being recorded, heard as it comes in
}

// UI thread, the device paused. The app's output callback sees the new rate and converts to it.
void SndstationPlatform::rate_moved(void* user, int rate) {
  auto* self = static_cast<SndstationPlatform*>(user);
  self->audio_info_.sample_rate = rate;
  self->log("audio: the device moved to %d Hz; the engine's output is converted to it", rate);
}

const char* SndstationPlatform::controller_name() const {
  return sp::calDevicePresent() ? sp::calDeviceName().c_str() : "keyboard";
}

bool SndstationPlatform::take_event(const SDL_Event& e) {
  sp::Event ev[16];
  const int n = sp::translate(e, pad_, ev);
  for (int i = 0; i < n; ++i) {
    // Vol+ and Vol- step the unit's volume, as in every sndstation program.
    if (ev[i].pressed && audio_open_ && (ev[i].b == sp::VolUp || ev[i].b == sp::VolDown))
      out_.stepVolume(ev[i].b == sp::VolUp ? 1 : -1);
  }
  switch (e.type) {
    case SDL_KEYDOWN: case SDL_KEYUP:
    case SDL_JOYAXISMOTION: case SDL_JOYBALLMOTION: case SDL_JOYHATMOTION:
    case SDL_JOYBUTTONDOWN: case SDL_JOYBUTTONUP:
    case SDL_JOYDEVICEADDED: case SDL_JOYDEVICEREMOVED:
    case SDL_CONTROLLERAXISMOTION: case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP:
    case SDL_CONTROLLERDEVICEADDED: case SDL_CONTROLLERDEVICEREMOVED: case SDL_CONTROLLERDEVICEREMAPPED:
      return true;
    default:
      return false;
  }
}

bool SndstationPlatform::sleep(std::string& why) { return sndstation::power::sleep(&why); }

// Once a frame: the buttons, and the headphone socket (a plug moves the sound between the speaker
// and the headphones, each at its own volume).
uint32_t SndstationPlatform::read_buttons() {
  if (audio_open_) out_.followJack();
  struct {
    sp::Button pad;
    uint32_t button;
  } const map[] = {
      {sp::Up, kUp},       {sp::Down, kDown},   {sp::Left, kLeft},     {sp::Right, kRight},
      {sp::East, kA},      {sp::South, kB},     {sp::North, kX},       {sp::West, kY},
      {sp::L1, kL},        {sp::R1, kR},        {sp::L2, kL2},         {sp::R2, kR2},
      {sp::Start, kStart}, {sp::Select, kSelect}, {sp::Menu, kMenu},
      // Not every unit has a Menu (FN) key; a stick click stands in for it.
      {sp::L3, kMenu},     {sp::R3, kMenu},
  };
  uint32_t b = 0;
  for (const auto& m : map)
    if (pad_.down(m.pad)) b |= m.button;
  return b;
}

// Recording.
std::vector<sndstation::audiocard::Card> SndstationPlatform::capture_cards() {
  std::vector<sndstation::audiocard::Card> out;
  if (!capture_allowed()) return out;
  for (const auto& c : sndstation::audiocard::list())
    if (c.capture && !c.virt && !c.display) out.push_back(c);
  // USB first: the default is a USB input when one is plugged in, as a built-in mic is rarely wanted
  // over one.
  std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.usb && !b.usb; });
  return out;
}

// "Hügelton Instruments Felucca at usb-5101400.usb-1, full speed": the part before " at ".
std::string SndstationPlatform::input_name(const sndstation::audiocard::Card& card) {
  std::string n = card.name.empty() ? card.id : card.name;
  const size_t at = n.find(" at ");
  if (at != std::string::npos) n.resize(at);
  return n;
}

bool SndstationPlatform::capture_available() { return !capture_cards().empty(); }

std::vector<std::string> SndstationPlatform::capture_devices() {
  std::vector<std::string> out;
  for (const auto& c : capture_cards()) out.push_back(input_name(c));
  return out;
}

bool SndstationPlatform::capture_open(const std::string& device, CaptureInfo& info, std::string& error) {
  capture_close();
  const std::vector<sndstation::audiocard::Card> cards = capture_cards();
  // The one named, or for the default the first (a USB input, if there is one).
  const sndstation::audiocard::Card* card = nullptr;
  for (const auto& c : cards)
    if (!card && (device.empty() || input_name(c) == device)) card = &c;
  if (!card) {
    error = cards.empty() ? "Nothing to record from" : "Can't find " + device;
    return false;
  }
  in_ready_ = false;
  in_.setOnBlock([this](const float* data, int frames, int channels, int, uint64_t) {
    if (in_ready_.load(std::memory_order_acquire) && channels == capture_info_channels())
      take_capture(data, size_t(frames) * size_t(channels));
  });
  if (!in_.open(*card)) {
    error = "Can't open " + input_name(*card) + ": " + in_.error();
    log("capture: %s", error.c_str());
    return false;
  }
  capture_begin(in_.rate(), in_.channels());
  in_ready_.store(true, std::memory_order_release);
  info = CaptureInfo{in_.rate(), std::max(1, in_.channels())};
  log("capture: \"%s\" (the SDK's input on card %s), %s, %d Hz, %d channels", input_name(*card).c_str(),
      in_.card().c_str(), in_.format().c_str(), in_.rate(), in_.channels());
  return true;
}

void SndstationPlatform::capture_close() {
  in_ready_ = false;
  if (in_.isOpen()) in_.close();
  capture_end();
}

}  // namespace twill
