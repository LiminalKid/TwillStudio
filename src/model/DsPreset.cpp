// The DecentSampler preset reader, written from the format's public documentation (its developer
// guide and schema). Reads the sample zones and what the sampler plays them with; everything else in
// a preset (effects, the UI, modulators...) is listed as unused.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

#include "model/Multisample.h"
#include "pugixml.hpp"

namespace twill {

namespace {

// The elements and attributes the reader can find in a preset, the way it reads them: an attribute
// set on <groups> or <group> applies to the samples below it unless they set their own.
struct Scope {
  pugi::xml_node groups, group, sample;
  // The most specific of the three that sets `name` (sample, then group, then groups).
  pugi::xml_attribute find(const char* name) const {
    for (const pugi::xml_node& n : {sample, group, groups})
      if (n)
        if (pugi::xml_attribute a = n.attribute(name)) return a;
    return {};
  }
  // The same for an attribute with a second spelling, which wins where both are set.
  pugi::xml_attribute find(const char* name, const char* alias) const {
    for (const pugi::xml_node& n : {sample, group, groups})
      if (n) {
        if (pugi::xml_attribute a = n.attribute(alias)) return a;
        if (pugi::xml_attribute a = n.attribute(name)) return a;
      }
    return {};
  }
};

float number(const pugi::xml_attribute& a, float fallback) {
  if (!a) return fallback;
  const char* s = a.value();
  char* end = nullptr;
  const float v = std::strtof(s, &end);
  return end == s ? fallback : v;
}

int whole(const pugi::xml_attribute& a, int fallback) { return int(std::lround(number(a, float(fallback)))); }

bool truth(const pugi::xml_attribute& a, bool fallback) {
  if (!a) return fallback;
  const std::string v = a.value();
  return v == "true" || v == "1" || v == "yes";
}

// A volume: linear ("0.5"), or in decibels ("-3dB", "3 dB").
float volume(const pugi::xml_attribute& a) {
  if (!a) return 1;
  const char* s = a.value();
  char* end = nullptr;
  const float v = std::strtof(s, &end);
  if (end == s) return 1;
  while (*end == ' ') ++end;
  if ((end[0] == 'd' || end[0] == 'D') && (end[1] == 'b' || end[1] == 'B')) return std::pow(10.0f, v / 20.0f);
  return std::max(0.0f, v);
}

SeqMode seq_mode(const pugi::xml_attribute& a) {
  if (!a) return SeqMode::Always;
  const std::string v = a.value();
  if (v == "round_robin") return SeqMode::RoundRobin;
  if (v == "random") return SeqMode::Random;
  if (v == "true_random") return SeqMode::TrueRandom;
  return SeqMode::Always;
}

std::string join(const std::string& folder, std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  if (!path.empty() && path[0] == '/') return path;
  while (path.rfind("./", 0) == 0) path.erase(0, 2);
  return folder + path;
}

}  // namespace

bool read_dspreset(const std::string& text, const std::string& folder, Multisample& out, std::string& error) {
  pugi::xml_document doc;
  const pugi::xml_parse_result parsed = doc.load_buffer(text.data(), text.size());
  if (!parsed) {
    error = std::string("not readable XML: ") + parsed.description();
    return false;
  }
  const pugi::xml_node root = doc.child("DecentSampler");
  if (!root) {
    error = "not a DecentSampler preset";
    return false;
  }
  out = Multisample();
  std::set<std::string> unused;
  // What a preset can have besides its sample zones, by the names a person would know them by.
  const std::map<std::string, std::string> sections = {
      {"effects", "effects"},       {"ui", "the controls"},           {"modulators", "modulators"},
      {"buses", "buses"},           {"midi", "MIDI mappings"},        {"tags", "tags"},
      {"noteSequences", "note sequences"}, {"arpeggiator", "the arpeggiator"}};
  for (const pugi::xml_node& n : root.children()) {
    const auto it = sections.find(n.name());
    if (it != sections.end() && n.first_child()) unused.insert(it->second);
  }
  // Attributes the reader doesn't play, wherever they're set.
  const std::map<std::string, std::string> skipped = {
      {"loCCN", "controller ranges"},  {"hiCCN", "controller ranges"},
      {"loChannel", "MIDI channel ranges"},  {"hiChannel", "MIDI channel ranges"},
      {"glideTime", "glide"},                {"glideMode", "glide"},          {"returnToHeldNote", "mono settings"},
      {"glideRetrigger", "mono settings"},   {"slices", "slices"},           {"beats", "tempo-synced loops"},
      {"silencedByTags", "voice muting"}};
  auto note_skipped = [&](const pugi::xml_node& n) {
    for (const pugi::xml_attribute& a : n.attributes()) {
      const auto it = skipped.find(a.name());
      if (it != skipped.end()) unused.insert(it->second);
    }
  };

  const pugi::xml_node groups = root.child("groups");
  note_skipped(groups);
  const float global_gain = volume(groups.attribute("globalVolume") ? groups.attribute("globalVolume") : groups.attribute("volume"));
  const float global_tune = number(groups.attribute("globalTuning"), 0);
  const float global_pan = number(groups.attribute("globalPan"), 0);
  const int global_transpose = whole(groups.attribute("globalTranspose"), 0);
  bool first_envelope = true;
  int group_index = -1;
  for (const pugi::xml_node& group : groups.children("group")) {
    ++group_index;
    note_skipped(group);
    if (!truth(group.attribute("enabled"), true)) continue;
    if (group.child("oscillator")) unused.insert("oscillators");
    const float group_gain = volume(group.attribute("groupVolume") ? group.attribute("groupVolume") : group.attribute("volume"));
    const float group_tune = number(group.attribute("groupTuning"), 0);
    const float group_pan = number(group.attribute("groupPan"), 0);
    const int transpose = global_transpose + whole(group.attribute("transpose"), 0);
    std::vector<size_t> in_group;
    for (const pugi::xml_node& sample : group.children("sample")) {
      note_skipped(sample);
      const Scope at{groups, group, sample};
      const std::string trigger = at.find("trigger") ? at.find("trigger").value() : "attack";
      if (trigger != "attack") {
        unused.insert(trigger == "release" ? "release triggers" : "first and legato triggers");
        continue;
      }
      const pugi::xml_attribute path = sample.attribute("path");
      if (!path || !*path.value()) continue;
      Zone z;
      z.sample = join(folder, path.value());
      z.root = whole(at.find("rootNote"), 60);
      z.lo_key = whole(at.find("loNote"), 0);
      z.hi_key = whole(at.find("hiNote"), 127);
      z.lo_vel = whole(at.find("loVel"), 0);
      z.hi_vel = whole(at.find("hiVel"), 127);
      // Transposing shifts the played note before mapping, which is the mapping shifted the
      // other way, at the sample's own pitch.
      z.root -= transpose;
      z.lo_key -= transpose;
      z.hi_key -= transpose;
      z.tune = global_tune + group_tune + number(sample.attribute("tuning"), 0);
      // The three volumes multiply; they don't inherit.
      z.gain = global_gain * group_gain * volume(sample.attribute("volume"));
      // Pan inherits, and the instrument's and the group's offsets are added to it.
      const float pan = number(at.find("pan"), 0) + global_pan + group_pan;
      z.pan = std::max(-1.0f, std::min(1.0f, pan / 100.0f));
      z.start = std::max(0, whole(sample.attribute("start"), 0));
      z.end = sample.attribute("end") ? whole(sample.attribute("end"), -1) : -1;
      z.loop = truth(at.find("loopEnabled"), false);
      if (sample.attribute("loopStart")) z.loop_start = whole(sample.attribute("loopStart"), -1);
      if (sample.attribute("loopEnd")) z.loop_end = whole(sample.attribute("loopEnd"), -1);
      z.loop_crossfade = std::max(0, whole(at.find("loopCrossfade"), 0));
      z.crossfade_linear = at.find("loopCrossfadeMode") && std::string(at.find("loopCrossfadeMode").value()) == "linear";
      z.keytrack = std::max(0.0f, number(at.find("pitchKeyTrack"), 1));
      z.tune_after_keytrack = truth(at.find("tuningIgnoresKeyTrack"), false);
      z.veltrack = std::max(0.0f, std::min(1.0f, number(at.find("ampVelTrack"), 1)));
      z.seq = seq_mode(at.find("seqMode"));
      z.seq_group = group_index;
      z.seq_length = std::max(0, whole(at.find("seqLength"), 0));
      z.seq_position = std::max(1, whole(at.find("seqPosition"), 1));
      // The amp envelope: the first group that sets one is the instrument's.
      const pugi::xml_attribute ea = at.find("attack", "ampEnvAttack"), ed = at.find("decay", "ampEnvDecay"),
                                es = at.find("sustain", "ampEnvSustain"), er = at.find("release", "ampEnvRelease");
      if (ea || ed || es || er) {
        Envelope e;
        e.attack = std::max(0.0f, number(ea, 0));
        e.decay = std::max(0.0f, number(ed, 0));
        e.sustain = std::max(0.0f, std::min(1.0f, number(es, 1)));
        e.release = std::max(0.0f, number(er, 0));
        if (first_envelope) {
          out.has_envelope = true;
          out.envelope = e;
          first_envelope = false;
        } else if (!(e == out.envelope)) {
          out.mixed_envelopes = true;
        }
      }
      if (z.hi_key < z.lo_key || z.hi_vel < z.lo_vel) continue;
      in_group.push_back(out.zones.size());
      out.zones.push_back(z);
    }
    // A round robin with no length given: as many turns as the group's highest position.
    int longest = 0;
    for (size_t i : in_group) longest = std::max(longest, out.zones[i].seq_position);
    for (size_t i : in_group)
      if (out.zones[i].seq == SeqMode::RoundRobin && out.zones[i].seq_length == 0) out.zones[i].seq_length = longest;
  }
  if (out.mixed_envelopes) unused.insert("other groups' envelopes");
  out.unused.assign(unused.begin(), unused.end());
  if (out.zones.empty()) {
    error = "the preset has no sample zones this app can play";
    return false;
  }
  return true;
}

}  // namespace twill
