// Loads a multisample preset and its samples on a thread of its own (design §6, "Multisamples"),
// so a big library doesn't stop the screen. One preset at a time; the app queues the rest.
// On a platform without threads (the web build) a load runs to the end inside start().
#pragma once
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <thread>

#include "engine/Sample.h"
#include "model/Multisample.h"
#include "platform/Platform.h"

namespace twill {

class PresetLoader {
 public:
  struct Job {
    std::string path;
    size_t budget = 0;  // bytes its new samples may take; 0: no limit
    // Samples already loaded, shared rather than read again.
    std::map<std::string, std::shared_ptr<const SampleData>> have;
  };
  struct Result {
    std::string path, error;  // error: why it didn't load (preset null)
    std::shared_ptr<Multisample> preset;
    // Every zone sample that loaded (those it shared too), and why the others didn't.
    std::map<std::string, std::shared_ptr<const SampleData>> samples;
    std::map<std::string, std::string> sample_errors;
    int missing = 0;
  };

  PresetLoader(Platform& p, bool threaded) : p_(p), threaded_(threaded) {}
  ~PresetLoader();

  void start(Job job);  // only while idle()
  bool idle() const { return !running_; }  // nothing started, or the result taken
  bool take(Result& out);  // the finished load's result, once; false while it runs
  void wait();             // until the load running finishes
  // The one loading: its path, and samples read so far of how many.
  const std::string& path() const { return job_.path; }
  int done() const { return done_.load(); }
  int total() const { return total_.load(); }

 private:
  void run();

  Platform& p_;
  const bool threaded_;
  bool running_ = false;
  Job job_;
  Result result_;
  std::thread thread_;
  std::atomic<bool> finished_{false};
  std::atomic<int> done_{0}, total_{0};
};

}  // namespace twill
