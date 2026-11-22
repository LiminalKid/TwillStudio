// CLAP instruments on sndstation (TWILL_PLUGINS): the platform's own host, the SDK's ClapPlayer,
// played by the engine a block at a time (ClapPlayer::process), and the plugins the launcher's
// PLUGINS scan found and rated (the platform's database, clapdb). Plugin tracks: platform/Plugins.h.
#include "platform/sndstation/SndstationPlatform.h"

#include <sndstation/clapdb.h>
#include <sndstation/clapplayer.h>
#include <sndstation/clapscan.h>

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

 private:
  std::unique_ptr<sc::ClapPlayer> p_;
};

class ClapHost : public PluginHost {
 public:
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
    auto p = std::make_unique<sc::ClapPlayer>();
    if (!p->load(where, byFile ? std::string() : id, &error)) return nullptr;
    if (!p->hasNoteInput()) {
      error = p->name() + " takes no notes: not an instrument";
      return nullptr;
    }
    if (!p->activate(rate, kMaxFrames, &error)) return nullptr;
    return std::make_unique<ClapInstance>(std::move(p));
  }
};

}  // namespace

PluginHost* SndstationPlatform::plugins() {
  if (!plugins_) plugins_ = std::make_unique<ClapHost>();
  return plugins_.get();
}

}  // namespace twill
