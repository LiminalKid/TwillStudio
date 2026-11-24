// CLAP instruments on sndstation (TWILL_PLUGINS): the platform's own host, the SDK's ClapPlayer,
// played by the engine a block at a time (ClapPlayer::process), and the plugins the launcher's
// PLUGINS scan found and rated (the platform's database, clapdb). Plugin tracks: platform/Plugins.h.
//
// A plugin made for the handheld (clap.embedded-hardware) can have a screen of its own: the unit
// is described to it as the launcher does (its buttons, but Menu, which Twill keeps, and the
// display), and while its screen is open Twill passes it the buttons and shows what it draws.
#include "platform/sndstation/SndstationPlatform.h"

#include <sndstation/buttons.h>
#include <sndstation/clapdb.h>
#include <sndstation/clapplayer.h>
#include <sndstation/clapscan.h>
#include <sndstation/embhw_device.h>
#include <sndstation/sysinfo.h>

#include <algorithm>
#include <sys/stat.h>

namespace twill {

namespace {

namespace sc = sndstation;

// A plugin the launcher's database has no id for (it never probed it) is known by its file.
constexpr const char* kByFile = "file:";

bool exists(const std::string& path) {
  struct stat st {};
  return !path.empty() && ::stat(path.c_str(), &st) == 0;
}

namespace eh = sndstation::embhw;
namespace sp = sndstation::pad;

// Twill's buttons, the pad's (as SndstationPlatform::read_buttons maps them) and the extension's.
struct Key {
  uint32_t button;
  sp::Button pad;
  uint32_t input;
};
constexpr Key kKeys[] = {
    {kUp, sp::Up, eh::DpadUp},         {kDown, sp::Down, eh::DpadDown},     {kLeft, sp::Left, eh::DpadLeft},
    {kRight, sp::Right, eh::DpadRight}, {kA, sp::East, eh::FaceEast},       {kB, sp::South, eh::FaceSouth},
    {kX, sp::North, eh::FaceNorth},     {kY, sp::West, eh::FaceWest},       {kL, sp::L1, eh::L1},
    {kR, sp::R1, eh::R1},               {kL2, sp::L2, eh::L2},              {kR2, sp::R2, eh::R2},
    {kStart, sp::Start, eh::Start},     {kSelect, sp::Select, eh::Select},
};

// The unit, for a plugin: the buttons Twill passes on, labelled as printed, and the display. No
// sticks (Twill reads none) and no Menu (Twill's, to come back from the plugin's screen).
eh::Device describe(Size screen) {
  eh::Device d;
  d.model = sndstation::sysfacts::boardModel();
  d.chip = sndstation::sysfacts::socName();
  d.screenW = uint32_t(std::max(0, screen.w));
  d.screenH = uint32_t(std::max(0, screen.h));
  for (const Key& k : kKeys) d.inputs.push_back({k.input, eh::IsButton, 0, sp::label(k.pad)});
  return d;
}

class ClapInstance : public PluginInstance {
 public:
  explicit ClapInstance(std::unique_ptr<sc::ClapPlayer> p) : p_(std::move(p)) {}
  // Freed only once the engine has let go of it (App::tend_plugins): nothing processes it now.
  ~ClapInstance() override { p_->unload(); }

  std::string name() const override { return p_->name(); }

  void process(const PluginEvent* events, int count, float* left, float* right, int frames) override {
    sc::ClapPlayer::Event ev[256];
    int n = 0;
    for (int i = 0; i < count && n < 256; ++i) {
      sc::ClapPlayer::Event& e = ev[n++];
      e.kind = events[i].velocity ? sc::ClapPlayer::Event::NoteOn : sc::ClapPlayer::Event::NoteOff;
      e.time = events[i].time;
      e.key = events[i].pitch;
      e.value = events[i].velocity / 127.0;
    }
    p_->process(ev, size_t(n), left, right, frames);
  }

  bool save_state(std::vector<uint8_t>& out) override { return p_->saveState(out, nullptr); }
  bool load_state(const std::vector<uint8_t>& in) override { return p_->loadState(in, nullptr); }

  std::vector<Param> params() override {
    std::vector<Param> v;
    for (const sc::ClapPlayer::Param& q : p_->params()) {
      if (q.hidden || q.readOnly) continue;
      Param p;
      p.id = q.id;
      p.name = q.name;
      p.min = q.min;
      p.max = q.max;
      p.def = q.def;
      p.value = q.value;
      p.stepped = q.stepped;
      v.push_back(std::move(p));
    }
    return v;
  }
  std::string param_text(uint32_t id, double value) override { return p_->paramText(id, value); }
  void set_param(uint32_t id, double value) override { p_->setParam(id, value); }
  void idle() override { p_->update(); }
  // The notes it holds, let go of at its next block (ClapPlayer's app queue).
  void released() override { p_->allNotesOff(); }

  bool has_screen() override { return p_->sndHasGui(); }

  bool open_screen(int max_w, int max_h, int& w, int& h) override {
    if (screen_ || !p_->sndHasGui() || max_w <= 0 || max_h <= 0) return false;
    eh::takeRequests(eh::Frame | eh::Quit);  // nothing left over from another plugin's screen
    uint32_t cw = 0, ch = 0;
    if (!p_->sndGuiCreate(uint32_t(max_w), uint32_t(max_h), cw, ch)) return false;
    p_->sndGuiShow(true);
    if (p_->sndWantsControls()) {  // its buttons start on a clean slate
      eh::InputEvent e;
      e.kind = eh::InputEvent::Kind::Reset;
      p_->sndSendInput(e);
    }
    w = int(w_ = cw);
    h = int(h_ = ch);
    sent_ = 0;
    screen_ = first_ = true;
    return true;
  }

  bool screen_frame(uint32_t held, uint32_t* pixels, int stride, bool& quit) override {
    quit = false;
    if (!screen_) return false;
    const uint32_t req = eh::takeRequests(eh::Frame | eh::Quit);
    quit = (req & eh::Quit) != 0;
    send(held);
    if (!(req & eh::Frame) && !first_) return false;
    first_ = false;
    return p_->sndGuiRender(pixels, w_, h_, uint32_t(stride) * 4, 0);
  }

  void close_screen() override {
    if (!screen_) return;
    send(0);  // every button it was given, let go of
    p_->sndGuiShow(false);
    p_->sndGuiDestroy();
    eh::takeRequests(eh::Frame | eh::Quit);
    screen_ = false;
  }

 private:
  // A press or release for each button that changed since the last call.
  void send(uint32_t held) {
    if (!p_->sndWantsControls()) {
      sent_ = held;
      return;
    }
    for (const Key& k : kKeys) {
      const bool now = (held & k.button) != 0, was = (sent_ & k.button) != 0;
      if (now == was || !eh::findInput(k.input)) continue;
      eh::InputEvent e;
      e.kind = now ? eh::InputEvent::Kind::Press : eh::InputEvent::Kind::Release;
      e.input = k.input;
      p_->sndSendInput(e);
    }
    sent_ = held;
  }

  std::unique_ptr<sc::ClapPlayer> p_;
  bool screen_ = false, first_ = false;
  uint32_t sent_ = 0;  // the buttons it was last told are held
  uint32_t w_ = 0, h_ = 0;  // the screen it draws
};

class ClapHost : public PluginHost {
 public:
  explicit ClapHost(Size screen) : screen_(screen) {}

  std::vector<PluginInfo> list() override {
    std::vector<PluginInfo> out;
    sc::clapdb::Database db;
    if (db.load(sc::clapdb::platformDatabasePath())) {
      for (const sc::clapdb::Listed& l : db.plugins(0)) {
        PluginInfo p;
        p.id = l.id.empty() ? kByFile + l.path : l.id;
        p.path = l.path;
        p.name = l.name;
        p.vendor = l.vendor;
        out.push_back(std::move(p));
      }
      if (!out.empty()) return out;
    }
    // No launcher's scan (a desk): every .clap there is, by its file, unchecked.
    for (const sc::clapscan::Entry& e : sc::clapscan::scan(sc::clapscan::defaultRoots())) {
      PluginInfo p;
      p.id = kByFile + e.path;
      p.path = e.path;
      p.name = e.fileName;
      out.push_back(std::move(p));
    }
    return out;
  }

  std::unique_ptr<PluginInstance> load(const std::string& id, const std::string& path, int rate,
                                       std::string& error) override {
    const bool byFile = id.rfind(kByFile, 0) == 0;
    std::string where = byFile ? id.substr(std::string(kByFile).size()) : path;
    if (!exists(where) && !byFile)  // moved or reinstalled: found again by its id
      for (const PluginInfo& p : list())
        if (p.id == id) where = p.path;
    if (!exists(where)) {
      error = "not installed";
      return nullptr;
    }
    // Described before it loads: a plugin asks about the unit from inside its init().
    eh::setDevice(describe(screen_));
    auto p = std::make_unique<sc::ClapPlayer>();
    if (!p->load(where, byFile ? std::string() : id, &error)) return nullptr;
    if (!p->hasNoteInput()) {
      error = p->name() + " takes no notes: not an instrument";
      return nullptr;
    }
    if (!p->activate(rate, kMaxFrames, &error)) return nullptr;
    return std::make_unique<ClapInstance>(std::move(p));
  }

 private:
  Size screen_;
};

}  // namespace

PluginHost* SndstationPlatform::plugins() {
  if (!plugins_) plugins_ = std::make_unique<ClapHost>(display_size());
  return plugins_.get();
}

}  // namespace twill
