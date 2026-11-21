// The SFZ reader, written from the format's public documentation (sfzformat.com). Reads the
// regions and what the sampler plays them with; everything else (filters, LFOs, effects, other
// envelopes, controller-driven sounds...) is listed as unused.
//
// A file is headers (<global>, <master>, <group>, <region>, <control>...) and opcodes (name=value).
// An opcode set under a header applies to every region below it until a header of the same level
// or higher comes along: <global> holds for the file, <master> until the next <master>, <group>
// until the next <group>, and a <region> is one zone.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>

#include "model/Multisample.h"

namespace twill {

namespace {

using Opcodes = std::map<std::string, std::string>;

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

// Comments out: "//" to the end of the line, and /* ... */.
std::string strip_comments(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') {
      while (i < s.size() && s[i] != '\n') ++i;
      if (i < s.size()) out += '\n';
    } else if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*') {
      i += 2;
      while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) {
        if (s[i] == '\n') out += '\n';
        ++i;
      }
      ++i;
    } else {
      out += s[i];
    }
  }
  return out;
}

// #define and #include, done to the text before it's read. Defines replace $NAME wherever it
// comes after them (longest names first, so $VEL doesn't eat $VEL2).
struct Preprocessor {
  const IncludeReader& includes;
  std::vector<std::pair<std::string, std::string>> defines;
  std::set<std::string>& unused;
  int depth = 0;

  std::string substitute(std::string line) const {
    for (const auto& d : defines) {
      size_t at = 0;
      while ((at = line.find(d.first, at)) != std::string::npos) {
        line.replace(at, d.first.size(), d.second);
        at += d.second.size();
      }
    }
    return line;
  }

  std::string run(const std::string& text) {
    std::string out;
    size_t pos = 0;
    const std::string s = strip_comments(text);
    while (pos <= s.size()) {
      size_t nl = s.find('\n', pos);
      if (nl == std::string::npos) nl = s.size();
      std::string line = s.substr(pos, nl - pos);
      pos = nl + 1;
      const std::string t = trim(line);
      if (t.rfind("#define", 0) == 0) {
        size_t a = 7;
        while (a < t.size() && std::isspace(static_cast<unsigned char>(t[a]))) ++a;
        size_t b = a;
        while (b < t.size() && !std::isspace(static_cast<unsigned char>(t[b]))) ++b;
        const std::string name = t.substr(a, b - a), value = trim(t.substr(b));
        if (name.size() > 1 && name[0] == '$') {
          defines.erase(std::remove_if(defines.begin(), defines.end(), [&](const auto& d) { return d.first == name; }),
                        defines.end());
          defines.emplace_back(name, value);
          std::sort(defines.begin(), defines.end(),
                    [](const auto& x, const auto& y) { return x.first.size() > y.first.size(); });
        }
        continue;
      }
      if (t.rfind("#include", 0) == 0) {
        const size_t q1 = t.find('"'), q2 = q1 == std::string::npos ? q1 : t.find('"', q1 + 1);
        std::string inc;
        if (q2 != std::string::npos && includes && depth < 8) {
          std::string path = substitute(t.substr(q1 + 1, q2 - q1 - 1));
          std::replace(path.begin(), path.end(), '\\', '/');
          if (includes(path, inc)) {
            ++depth;
            out += run(inc);
            --depth;
            out += '\n';
            continue;
          }
        }
        unused.insert("included files that couldn't be read");
        continue;
      }
      out += substitute(line);
      out += '\n';
      if (nl == s.size()) break;
    }
    return out;
  }
};

// A header or an opcode, in file order.
struct Token {
  bool header;
  std::string name, value;
};

bool is_name_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Opcodes run to the next opcode, header or line end, so a sample's path may hold spaces.
std::vector<Token> tokenize(const std::string& s) {
  std::vector<Token> out;
  size_t i = 0;
  const size_t n = s.size();
  while (i < n) {
    const char c = s[i];
    if (std::isspace(static_cast<unsigned char>(c))) {
      ++i;
    } else if (c == '<') {
      const size_t e = s.find('>', i);
      if (e == std::string::npos) break;
      out.push_back({true, trim(s.substr(i + 1, e - i - 1)), ""});
      i = e + 1;
    } else if (is_name_char(c)) {
      size_t e = i;
      while (e < n && is_name_char(s[e])) ++e;
      if (e >= n || s[e] != '=') {  // not an opcode: skip the word
        i = e + 1;
        continue;
      }
      const std::string name = s.substr(i, e - i);
      size_t v = e + 1, end = v;
      // The value ends where a line ends, a header starts, or whitespace is followed by name=.
      while (end < n && s[end] != '\n' && s[end] != '\r' && s[end] != '<') {
        if (std::isspace(static_cast<unsigned char>(s[end]))) {
          size_t k = end;
          while (k < n && (s[k] == ' ' || s[k] == '\t')) ++k;
          size_t w = k;
          while (w < n && is_name_char(s[w])) ++w;
          if (w > k && w < n && s[w] == '=') break;
        }
        ++end;
      }
      out.push_back({false, name, trim(s.substr(v, end - v))});
      i = end;
    } else {
      ++i;
    }
  }
  return out;
}

// A note, as a number or a name ("c4" is 60, "c#4" and "db4" 61), with the file's offsets.
int note_value(const std::string& v, int offset, int fallback) {
  if (v.empty()) return fallback;
  if (std::isdigit(static_cast<unsigned char>(v[0])) || v[0] == '-') {
    char* end = nullptr;
    const long k = std::strtol(v.c_str(), &end, 10);
    return end == v.c_str() ? fallback : int(k) + offset;
  }
  static const int base[7] = {9, 11, 0, 2, 4, 5, 7};  // a b c d e f g
  const char letter = char(std::tolower(static_cast<unsigned char>(v[0])));
  if (letter < 'a' || letter > 'g') return fallback;
  int k = base[letter - 'a'];
  size_t i = 1;
  if (i < v.size() && v[i] == '#') ++k, ++i;
  else if (i < v.size() && v[i] == 'b' && i + 1 < v.size() && (std::isdigit(static_cast<unsigned char>(v[i + 1])) || v[i + 1] == '-'))
    --k, ++i;
  char* end = nullptr;
  const long octave = std::strtol(v.c_str() + i, &end, 10);
  if (end == v.c_str() + i) return fallback;
  return int(octave + 1) * 12 + k + offset;
}

float number(const Opcodes& o, const char* name, float fallback) {
  auto it = o.find(name);
  if (it == o.end()) return fallback;
  char* end = nullptr;
  const float v = std::strtof(it->second.c_str(), &end);
  return end == it->second.c_str() ? fallback : v;
}

const std::string* text(const Opcodes& o, const char* name) {
  auto it = o.find(name);
  return it == o.end() ? nullptr : &it->second;
}

// What the reader doesn't play, by an opcode's name (or its start), as a person would know it.
const char* unused_kind(const std::string& op) {
  struct Kind {
    const char* prefix;
    const char* what;
  };
  static const Kind kinds[] = {
      {"fil", "filters"},           {"cutoff", "filters"},          {"resonance", "filters"},
      {"eq", "EQ"},                 {"lfo", "LFOs"},                {"amplfo", "LFOs"},
      {"pitchlfo", "LFOs"},         {"fillfo", "LFOs"},             {"eg", "extra envelopes"},
      {"pitcheg", "extra envelopes"}, {"fileg", "extra envelopes"}, {"ampeg_vel", "velocity-shaped envelopes"},
      {"effect", "effect sends"},   {"bend", "pitch bend"},         {"xf", "crossfades"},
      {"amp_velcurve", "velocity curves"}, {"amp_keytrack", "level key tracking"},
      {"amp_keycenter", "level key tracking"}, {"group", "voice muting"}, {"off_", "voice muting"},
      {"polyphony", "polyphony limits"}, {"note_polyphony", "polyphony limits"}, {"delay", "note delays"},
      {"width", "stereo width"},    {"position", "stereo width"},   {"lochan", "MIDI channel ranges"},
      {"hichan", "MIDI channel ranges"}, {"lobend", "pitch bend ranges"}, {"hibend", "pitch bend ranges"},
      {"lochanaft", "aftertouch ranges"}, {"hichanaft", "aftertouch ranges"}, {"lopolyaft", "aftertouch ranges"},
      {"hipolyaft", "aftertouch ranges"}, {"lobpm", "tempo ranges"},  {"hibpm", "tempo ranges"},
      {"loprog", "program ranges"}, {"hiprog", "program ranges"},  {"rt_decay", "release trigger decay"},
      {"direction", "reversed regions"}, {"pitch_random", "random pitch"}, {"amp_random", "random level"},
      {"pitch_veltrack", "velocity pitch tracking"}, {"tune_", "controller tuning"},
      {"pitch_on", "controller tuning"}, {"volume_on", "controller levels"}, {"amplitude_on", "controller levels"},
      {"pan_on", "controller pan"}, {"offset_", "controller sample offsets"}, {"sample_dyn", "controller sample offsets"},
      {"sustain_", "sustain pedal settings"},
      {"sostenuto", "sustain pedal settings"}, {"count", "play counts"}, {"sync", "tempo-synced regions"},
      {"oscillator", "oscillators"}, {"sw_vel", "key switches"}, {"sw_label", "key switches"},
      {"label_", "labels"},  {"image", "the controls"}, {"set_", "controller defaults"},
      {"ampeg_attackcc", "controller envelopes"}, {"ampeg_releasecc", "controller envelopes"},
      {"ampeg_decaycc", "controller envelopes"}, {"ampeg_sustaincc", "controller envelopes"},
      {"ampeg_holdcc", "controller envelopes"}, {"ampeg_delaycc", "controller envelopes"},
      {"ampeg_startcc", "controller envelopes"}, {"ampeg_dynamic", "controller envelopes"},
  };
  for (const Kind& k : kinds)
    if (op.compare(0, std::char_traits<char>::length(k.prefix), k.prefix) == 0) return k.what;
  return nullptr;
}

// The opcodes the reader plays (others are listed as unused).
const std::set<std::string>& played() {
  static const std::set<std::string> s = {
      "sample", "lokey", "hikey", "key", "pitch_keycenter", "lovel", "hivel", "tune", "pitch", "transpose",
      "pitch_keytrack", "volume", "amplitude", "gain", "global_volume", "master_volume", "group_volume",
      "global_amplitude", "master_amplitude", "group_amplitude", "pan", "offset", "end", "loop_mode",
      "loopmode", "loop_start", "loopstart", "loop_end", "loopend", "seq_length", "seq_position", "lorand",
      "hirand", "trigger", "ampeg_attack", "ampeg_decay", "ampeg_sustain", "ampeg_release", "amp_veltrack",
      "sw_last", "sw_lokey", "sw_hikey", "sw_default", "sw_lolast", "sw_hilast", "sw_up", "sw_down",
      "sw_previous", "default_path", "note_offset", "octave_offset", "ampeg_hold", "ampeg_delay",
      "ampeg_start", "loop_crossfade", "loopcrossfade", "region_label", "group_label", "master_label", "global_label", "key_label", "hint_",
  };
  return s;
}

bool starts(const std::string& s, const char* p) { return s.compare(0, std::char_traits<char>::length(p), p) == 0; }

}  // namespace

bool read_sfz(const std::string& source, const std::string& folder, Multisample& out, std::string& error,
              const IncludeReader& includes) {
  out = Multisample();
  std::set<std::string> unused;
  Preprocessor pre{includes, {}, unused};
  const std::vector<Token> tokens = tokenize(pre.run(source));

  // The file's settings (<control>): where samples are, note offsets, controller starting values.
  std::string default_path;
  int note_offset = 0;
  std::map<int, float> cc;  // set_ccN; others are 0

  // The opcodes in force at each level, and the region being read.
  Opcodes global, master, group, region;
  enum class At { None, Control, Global, Master, Group, Region, Other } at = At::None;
  int group_index = 0;
  bool first_envelope = true;
  std::vector<std::pair<Zone, std::string>> zones;  // with the key switch each answers ("" if none)
  std::set<int> switch_keys;
  int sw_default = -1;

  auto finish_region = [&] {
    if (at != At::Region) return;
    // The region's opcodes over its group's, over its master's, over the global ones.
    Opcodes o = global;
    for (const Opcodes* layer : {&master, &group, &region})
      for (const auto& kv : *layer) o[kv.first] = kv.second;
    region.clear();
    for (const auto& kv : o) {
      if (played().count(kv.first) || starts(kv.first, "hint_") || starts(kv.first, "label")) continue;
      if (starts(kv.first, "locc") || starts(kv.first, "hicc") || starts(kv.first, "on_locc") || starts(kv.first, "on_hicc") ||
          starts(kv.first, "start_locc") || starts(kv.first, "start_hicc") || starts(kv.first, "stop_locc") || starts(kv.first, "stop_hicc"))
        continue;  // below
      const char* kind = unused_kind(kv.first);
      unused.insert(kind ? kind : "other settings");
    }
    const std::string* sample = text(o, "sample");
    if (!sample || sample->empty()) return;
    if ((*sample)[0] == '*') {  // *sine, *noise...: built-in sounds, not files
      unused.insert("built-in waveforms");
      return;
    }
    // Played by a controller, not a key: not played.
    for (const auto& kv : o)
      if (starts(kv.first, "on_locc") || starts(kv.first, "on_hicc") || starts(kv.first, "start_locc") || starts(kv.first, "start_hicc")) {
        unused.insert("regions a controller plays");
        return;
      }
    // Controller ranges: played as if the controls were where the file starts them (at rest).
    for (const auto& kv : o) {
      const bool lo = starts(kv.first, "locc"), hi = starts(kv.first, "hicc");
      if (!lo && !hi) continue;
      const int num = std::atoi(kv.first.c_str() + 4);
      const float value = cc.count(num) ? cc[num] : 0;
      const float bound = float(std::atof(kv.second.c_str()));
      unused.insert("controller ranges");
      if ((lo && value < bound) || (hi && value > bound)) return;
    }
    const std::string trig = text(o, "trigger") ? *text(o, "trigger") : "attack";
    if (trig != "attack") {
      unused.insert(trig == "release" || trig == "release_key" ? "release triggers" : "first and legato triggers");
      return;
    }
    if (number(o, "ampeg_hold", 0) > 0 || number(o, "ampeg_delay", 0) > 0 || number(o, "ampeg_start", 0) > 0)
      unused.insert("envelope hold and delay");
    Zone z;
    std::string path = default_path + *sample;
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.rfind("./", 0) == 0) path.erase(0, 2);
    z.sample = !path.empty() && path[0] == '/' ? path : folder + path;
    const int key = note_value(text(o, "key") ? *text(o, "key") : "", note_offset, -1);
    z.lo_key = note_value(text(o, "lokey") ? *text(o, "lokey") : "", note_offset, key >= 0 ? key : 0);
    z.hi_key = note_value(text(o, "hikey") ? *text(o, "hikey") : "", note_offset, key >= 0 ? key : 127);
    z.root = note_value(text(o, "pitch_keycenter") ? *text(o, "pitch_keycenter") : "", note_offset, key >= 0 ? key : 60);
    z.lo_vel = int(number(o, "lovel", 1));
    z.hi_vel = int(number(o, "hivel", 127));
    if (z.lo_vel <= 1) z.lo_vel = 0;  // velocity 0 isn't a note; the app's lowest is the format's 1
    // Tuning: cents, and transpose in semitones; both change the pitch, not which keys play.
    z.tune = number(o, "tune", number(o, "pitch", 0)) / 100.0f + number(o, "transpose", 0);
    z.tune_after_keytrack = true;
    z.keytrack = std::max(0.0f, number(o, "pitch_keytrack", 100) / 100.0f);
    // Levels: each level's volume (dB) and amplitude (%) multiply.
    float db = number(o, "volume", 0) + number(o, "gain", 0);
    for (const char* v : {"global_volume", "master_volume", "group_volume"}) db += number(o, v, 0);
    float amp = number(o, "amplitude", 100) / 100.0f;
    for (const char* a : {"global_amplitude", "master_amplitude", "group_amplitude"}) amp *= number(o, a, 100) / 100.0f;
    z.gain = std::max(0.0f, std::pow(10.0f, db / 20.0f) * amp);
    z.pan = std::max(-1.0f, std::min(1.0f, number(o, "pan", 0) / 100.0f));
    z.veltrack = std::max(0.0f, std::min(1.0f, number(o, "amp_veltrack", 100) / 100.0f));
    z.start = std::max(0, int(number(o, "offset", 0)));
    const float end = number(o, "end", -1);
    z.end = end > 0 ? int(end) + 1 : -1;  // the format's end is the last frame played
    // Loops: the file's own markers loop unless told not to.
    const std::string* lm = text(o, "loop_mode") ? text(o, "loop_mode") : text(o, "loopmode");
    if (!lm) z.loop_if_marked = true;
    else if (*lm == "loop_continuous" || *lm == "loop_sustain") z.loop = true;
    else if (*lm == "one_shot") z.one_shot = true;
    if (lm && *lm == "loop_sustain") unused.insert("sustain loops (played as continuous loops)");
    const float ls = number(o, "loop_start", number(o, "loopstart", -1)), le = number(o, "loop_end", number(o, "loopend", -1));
    if (ls >= 0) z.loop_start = int(ls);
    if (le >= 0) z.loop_end = int(le);
    z.loop_crossfade_s = std::max(0.0f, number(o, "loop_crossfade", number(o, "loopcrossfade", 0)));
    // Round robins and random choices.
    const int seq_length = int(number(o, "seq_length", 1));
    if (seq_length > 1) {
      z.seq = SeqMode::RoundRobin;
      z.seq_length = seq_length;
      z.seq_position = std::max(1, int(number(o, "seq_position", 1)));
    }
    z.seq_group = group_index;
    z.lo_rand = std::max(0.0f, number(o, "lorand", 0));
    z.hi_rand = std::min(1.0f, number(o, "hirand", 1));
    // The amp envelope: the first region that sets one is the instrument's.
    const bool has_env = o.count("ampeg_attack") || o.count("ampeg_decay") || o.count("ampeg_sustain") || o.count("ampeg_release");
    if (has_env) {
      Envelope e;
      e.attack = std::max(0.0f, number(o, "ampeg_attack", 0));
      e.decay = std::max(0.0f, number(o, "ampeg_decay", 0));
      e.sustain = std::max(0.0f, std::min(1.0f, number(o, "ampeg_sustain", 100) / 100.0f));
      e.release = std::max(0.0f, number(o, "ampeg_release", 0.001f));
      if (first_envelope) {
        out.has_envelope = true;
        out.envelope = e;
        first_envelope = false;
      } else if (!(e == out.envelope)) {
        out.mixed_envelopes = true;
      }
    }
    if (z.hi_key < z.lo_key || z.hi_vel < z.lo_vel || z.hi_rand <= z.lo_rand) return;
    // Key switches: which switch the region answers, the default chosen below.
    std::string sw;
    if (const std::string* s = text(o, "sw_last")) {
      const int k = note_value(*s, note_offset, -1);
      sw = std::to_string(k);
      switch_keys.insert(k);
    }
    if (const std::string* s = text(o, "sw_default")) sw_default = note_value(*s, note_offset, -1);
    if (text(o, "sw_down") || text(o, "sw_up") || text(o, "sw_previous")) unused.insert("key switches");
    zones.emplace_back(z, sw);
  };

  for (const Token& t : tokens) {
    if (t.header) {
      finish_region();
      if (t.name == "region") {
        at = At::Region;
      } else if (t.name == "group") {
        at = At::Group;
        group.clear();
        ++group_index;
      } else if (t.name == "master") {
        at = At::Master;
        master.clear();
        group.clear();
        ++group_index;
      } else if (t.name == "global") {
        at = At::Global;
        global.clear();
        master.clear();
        group.clear();
        ++group_index;
      } else if (t.name == "control") {
        at = At::Control;
      } else {
        at = At::Other;
        if (t.name == "effect") unused.insert("effects");
        else if (t.name == "curve") unused.insert("velocity curves");
        else if (t.name == "midi") unused.insert("MIDI settings");
        else if (t.name == "sample") unused.insert("embedded samples");
        else unused.insert("other settings");
      }
      continue;
    }
    switch (at) {
      case At::Control:
        if (t.name == "default_path") {
          default_path = t.value;
          std::replace(default_path.begin(), default_path.end(), '\\', '/');
          if (!default_path.empty() && default_path.back() != '/') default_path += '/';
        } else if (t.name == "note_offset") {
          note_offset += std::atoi(t.value.c_str());
        } else if (t.name == "octave_offset") {
          note_offset += 12 * std::atoi(t.value.c_str());
        } else if (starts(t.name, "set_cc") || starts(t.name, "set_hdcc")) {
          const bool hd = starts(t.name, "set_hdcc");
          const int num = std::atoi(t.name.c_str() + (hd ? 8 : 6));
          const float v = float(std::atof(t.value.c_str()));
          cc[num] = hd ? v * 127 : v;
        } else if (!starts(t.name, "label_cc") && !starts(t.name, "hint_")) {
          unused.insert("other settings");
        }
        break;
      case At::Global: global[t.name] = t.value; break;
      case At::Master: master[t.name] = t.value; break;
      case At::Group: group[t.name] = t.value; break;
      case At::Region: region[t.name] = t.value; break;
      case At::None: region[t.name] = t.value, at = At::Region; break;  // opcodes before any header
      case At::Other: break;
    }
  }
  finish_region();

  // Key switches: only the default articulation plays (the file's, else the lowest switch).
  int keep = sw_default;
  if (!switch_keys.empty() && (keep < 0 || !switch_keys.count(keep))) keep = *switch_keys.begin();
  if (!switch_keys.empty()) unused.insert("key switches (only the first articulation plays)");
  for (auto& zs : zones)
    if (zs.second.empty() || zs.second == std::to_string(keep)) out.zones.push_back(zs.first);

  if (out.mixed_envelopes) unused.insert("other regions' envelopes");
  out.unused.assign(unused.begin(), unused.end());
  if (out.zones.empty()) {
    error = "the file has no regions this app can play";
    return false;
  }
  return true;
}

}  // namespace twill
