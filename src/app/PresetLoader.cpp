#include "app/PresetLoader.h"

#include <algorithm>
#include <cstdio>

namespace twill {

PresetLoader::~PresetLoader() {
  if (thread_.joinable()) thread_.join();
}

void PresetLoader::start(Job job) {
  if (running_) return;
  if (thread_.joinable()) thread_.join();
  job_ = std::move(job);
  result_ = Result();
  result_.path = job_.path;
  done_ = 0;
  total_ = 0;
  finished_ = false;
  running_ = true;
  if (threaded_) thread_ = std::thread([this] { run(); });
  else run();
}

bool PresetLoader::take(Result& out) {
  if (!running_ || !finished_.load()) return false;
  if (thread_.joinable()) thread_.join();
  out = std::move(result_);
  result_ = Result();
  job_.have.clear();
  running_ = false;
  return true;
}

void PresetLoader::wait() {
  if (thread_.joinable()) thread_.join();
}

void PresetLoader::run() {
  Result& r = result_;
  auto finish = [&](const std::string& error) {
    if (!error.empty()) {
      r.error = error;
      r.preset.reset();
      r.samples.clear();
    }
    finished_ = true;
  };
  std::vector<uint8_t> bytes;
  if (!p_.read_file(job_.path.c_str(), bytes)) return finish("Can't read " + job_.path);
  auto m = std::make_shared<Multisample>();
  std::string err;
  Platform& p = p_;
  const IncludeReader includes = [&p](const std::string& path, std::string& text) {
    std::vector<uint8_t> b;
    if (!p.read_file(path.c_str(), b)) return false;
    text.assign(b.begin(), b.end());
    return true;
  };
  if (!read_multisample(job_.path, std::string(bytes.begin(), bytes.end()), *m, err, includes)) return finish(err);
  r.preset = m;
  // Each sample once, though several zones may play it.
  std::vector<std::string> files;
  for (const Zone& z : m->zones)
    if (!r.samples.count(z.sample) && std::find(files.begin(), files.end(), z.sample) == files.end())
      files.push_back(z.sample);
  total_ = int(files.size());
  size_t added = 0;
  for (const std::string& f : files) {
    auto had = job_.have.find(f);
    if (had != job_.have.end()) {
      r.samples[f] = had->second;
    } else {
      std::string why;
      std::shared_ptr<const SampleData> s;
      if (!p_.read_file(f.c_str(), bytes)) why = "Can't read " + f;
      else s = decode_sample(bytes.data(), bytes.size(), f.substr(f.find_last_of('/') + 1), why);
      if (s) {
        added += s->bytes();
        r.samples[f] = s;
      } else {
        r.sample_errors[f] = why;
      }
      if (job_.budget && added > job_.budget) {
        char b[160];
        std::snprintf(b, sizeof b, "Too big for this device: its samples need more than the %zu MB left for samples",
                      job_.budget >> 20);
        return finish(b);
      }
    }
    ++done_;
  }
  for (const Zone& z : m->zones) r.missing += r.samples.count(z.sample) ? 0 : 1;
  if (r.missing == int(m->zones.size())) return finish("None of its samples could be read");
  finish("");
}

}  // namespace twill
