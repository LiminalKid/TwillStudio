// What the preset formats share: the zone model's helpers, and choosing a reader by file name.
#include <algorithm>
#include <cctype>

#include "model/Multisample.h"

namespace twill {

namespace {

std::string lower_extension(const std::string& name) {
  const size_t dot = name.find_last_of('.');
  if (dot == std::string::npos) return "";
  std::string e = name.substr(dot);
  for (char& c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
  return e;
}

}  // namespace

bool is_multisample_file(const std::string& name) {
  const std::string e = lower_extension(name);
  return e == ".dspreset" || e == ".sfz";
}

bool read_multisample(const std::string& path, const std::string& text, Multisample& out, std::string& error,
                      const IncludeReader& includes) {
  const size_t slash = path.find_last_of('/');
  const std::string folder = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
  const std::string e = lower_extension(path);
  bool ok;
  if (e == ".sfz") {
    // Includes are relative to the file's folder.
    IncludeReader in;
    if (includes) in = [&](const std::string& p, std::string& t) { return includes(!p.empty() && p[0] == '/' ? p : folder + p, t); };
    ok = read_sfz(text, folder, out, error, in);
  } else if (e == ".dspreset") {
    ok = read_dspreset(text, folder, out, error);
  } else {
    error = "not a preset this app reads";
    return false;
  }
  if (!ok) return false;
  out.name = path.substr(slash == std::string::npos ? 0 : slash + 1);
  out.name = out.name.substr(0, out.name.find_last_of('.'));
  return true;
}

bool Zone::operator==(const Zone& o) const {
  return sample == o.sample && lo_key == o.lo_key && hi_key == o.hi_key && lo_vel == o.lo_vel && hi_vel == o.hi_vel &&
         root == o.root && tune == o.tune && gain == o.gain && pan == o.pan && start == o.start && end == o.end &&
         loop == o.loop && loop_if_marked == o.loop_if_marked && loop_start == o.loop_start &&
         loop_end == o.loop_end && loop_crossfade == o.loop_crossfade && loop_crossfade_s == o.loop_crossfade_s &&
         crossfade_linear == o.crossfade_linear && one_shot == o.one_shot && keytrack == o.keytrack &&
         tune_after_keytrack == o.tune_after_keytrack && veltrack == o.veltrack && lo_rand == o.lo_rand &&
         hi_rand == o.hi_rand && seq == o.seq &&
         seq_group == o.seq_group && seq_length == o.seq_length && seq_position == o.seq_position;
}

int Multisample::lowest_key() const {
  int k = 127;
  for (const Zone& z : zones) k = std::min(k, z.lo_key);
  return std::max(0, k);
}

int Multisample::highest_key() const {
  int k = 0;
  for (const Zone& z : zones) k = std::max(k, z.hi_key);
  return std::min(127, k);
}

}  // namespace twill
