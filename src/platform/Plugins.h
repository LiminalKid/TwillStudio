// Instruments a platform hosts (CLAP on sndstation): what a Plugin track plays. Built only with
// TWILL_PLUGINS (a sndstation build); every other build has no Plugin tracks. The platform lists
// what is installed and loads one; the engine plays it a block at a time on the audio thread; the
// app lists and sets its parameters and keeps its state in the project. Nothing outside
// src/platform/ knows what kind of plugin it is.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace twill {

// A note for a plugin, `time` frames into the block process() is given.
struct PluginEvent {
  uint32_t time = 0;
  uint8_t pitch = 0;
  uint8_t velocity = 0;  // 0: note off
};

class PluginInstance {
 public:
  virtual ~PluginInstance() = default;
  virtual std::string name() const = 0;

  // Audio thread: `frames` (at most PluginHost::kMaxFrames) into `left` and `right`, overwriting
  // them, with `events` first, in time order.
  virtual void process(const PluginEvent* events, int count, float* left, float* right, int frames) = 0;

  // UI thread. Its state, as bytes the project keeps.
  virtual bool save_state(std::vector<uint8_t>& out) = 0;
  virtual bool load_state(const std::vector<uint8_t>& in) = 0;
  // Its parameters, in its order: what the Sound page shows and moves.
  struct Param {
    uint32_t id = 0;
    std::string name;
    double min = 0, max = 1, def = 0, value = 0;
    bool stepped = false;
  };
  virtual std::vector<Param> params() = 0;
  virtual std::string param_text(uint32_t id, double value) = 0;  // "" if it gives none
  virtual void set_param(uint32_t id, double value) = 0;
  // UI thread, every frame: what the plugin asked to do on it.
  virtual void idle() {}
  // UI thread: the engine has stopped playing it (no snapshot has it any more, and the audio thread
  // has let go). Notes it still holds are let go of before it plays again.
  virtual void released() {}
};

struct PluginInfo {
  std::string id;    // the plugin's own id: what a project keeps
  std::string path;  // where it is installed now
  std::string name, vendor;
};

class PluginHost {
 public:
  static constexpr int kMaxFrames = 4096;  // the largest block process() is ever given
  virtual ~PluginHost() = default;
  // What is installed and plays, sorted for a list.
  virtual std::vector<PluginInfo> list() = 0;
  // Loaded and ready to play at `rate`: found by `id` if `path` is not it any more. Null, with why.
  virtual std::unique_ptr<PluginInstance> load(const std::string& id, const std::string& path, int rate,
                                               std::string& error) = 0;
};

}  // namespace twill
