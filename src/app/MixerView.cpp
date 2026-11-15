#include "app/MixerView.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace twill {

namespace {
constexpr float kLabelW = 64, kNameH = 30, kLevelH = 118, kMinLevelH = 80, kRowH = 28;
constexpr int kRows = 6;  // under the level: pan, three sends, mute, solo
const char* const kRowNames[] = {"Level", "Pan", "Chorus", "Delay", "Reverb", "Mute", "Solo"};
const char* const kMasterRowNames[] = {"Level", "EQ low", "EQ mid", "EQ high", "", "", ""};
}  // namespace

MixerView::Layout MixerView::layout() const {
  const Rect c = app_.content_rect();
  Layout l;
  // On a short or narrow screen (the RG28XX at 1.5x) the rows and labels give up some room so
  // every row still fits; the meters take what is left.
  const float avail_w = c.w / app_.u(1), avail_h = c.h / app_.u(1) - 4 - kNameH;
  const float label_units = std::min(kLabelW, avail_w * 0.125f);
  const float row_units = std::min(kRowH, std::max(20.0f, (avail_h - kMinLevelH) / kRows));
  const float level_units = std::min(kLevelH, avail_h - kRows * row_units);
  const int label_w = int(app_.u(label_units));
  l.labels = Rect{c.x, c.y, label_w, c.h};
  l.x0 = c.x + label_w;
  l.strip_w = (c.w - label_w) / std::max(1, strips());
  l.name_h = int(app_.u(kNameH));
  l.level_h = int(app_.u(level_units));
  l.row_h = int(app_.u(row_units));
  l.top = c.y + int(app_.u(4));
  return l;
}

Rect MixerView::strip_rect(const Layout& l, int strip) const {
  const Rect c = app_.content_rect();
  return Rect{l.x0 + strip * l.strip_w, c.y, l.strip_w, c.h};
}

ParamId MixerView::param_at(int strip, int row) const {
  if (is_master(strip)) {
    switch (row) {
      case kLevel: return kParamMasterLevel;
      case kPan: return kParamEqLow;
      case kChorus: return kParamEqMid;
      case kDelay: return kParamEqHigh;
      default: return kParamNone;
    }
  }
  switch (row) {
    case kLevel: return kParamLevel;
    case kPan: return kParamPan;
    case kChorus: return kParamSendChorus;
    case kDelay: return kParamSendDelay;
    case kReverb: return kParamSendReverb;
    default: return kParamNone;
  }
}

void MixerView::open_effects() {
  App& app = app_;
  std::vector<InspectorField> fields;
  for (const ParamInfo& info : effect_params()) {
    const ParamId id = info.id;
    fields.push_back({info.name, [&app, id] { return format_param(id, app.project().mix.get(id)); },
                      [&app, id, &info](int step, bool coarse) {
                        const float span = info.max - info.min;
                        const float delta = id == kParamDelayTime ? 1.0f / 6
                                            : info.integer             ? 1.0f
                                                                       : span * (coarse ? 0.1f : 0.02f);
                        app.history().set_project_param(id, app.project().mix.get(id) + step * delta);
                      }});
  }
  app.open_inspector("Effects", std::move(fields));
}

void MixerView::handle_input() {
  const InputTracker& in = app_.input();
  Project& p = app_.project();
  EditHistory& h = app_.history();
  const int n = strips();
  strip_ = std::min(strip_, n - 1);
  bool changed = false;

  if (in.pressed(kX)) {
    open_effects();
    return;
  }
  if (in.pressed(kA)) {
    a_used_ = false;
    // Mute and solo switch on a press.
    if (!is_master(strip_) && (row_ == kMute || row_ == kSolo)) {
      const Track& t = p.tracks[size_t(strip_)];
      h.set_track_flags(strip_, row_ == kMute ? !t.mute : t.mute, row_ == kSolo ? !t.solo : t.solo, t.voices);
      changed = true;
    }
  }
  if (in.held(kA)) {
    const int step = in.pressed_or_repeat(kUp | kRight) ? 1 : in.pressed_or_repeat(kDown | kLeft) ? -1 : 0;
    const ParamId id = param_at(strip_, row_);
    if (step && id != kParamNone) {
      const ParamInfo* info = param_info(id);
      const float delta = step * (info->max - info->min) * (in.held(kR) ? 0.1f : 0.02f);
      if (is_master(strip_)) h.set_project_param(id, p.mix.get(id) + delta);
      else h.set_track_param(strip_, id, p.track_param(strip_, id) + delta);
      a_used_ = changed = true;
    }
  } else {
    if (in.pressed_or_repeat(kLeft)) strip_ = (strip_ + n - 1) % n, changed = true;
    if (in.pressed_or_repeat(kRight)) strip_ = (strip_ + 1) % n, changed = true;
    const int rows = is_master(strip_) ? 4 : kNumRows;
    if (in.pressed_or_repeat(kUp)) row_ = (row_ + rows - 1) % rows, changed = true;
    if (in.pressed_or_repeat(kDown)) row_ = (row_ + 1) % rows, changed = true;
    if (row_ >= rows) row_ = rows - 1;
  }
  if (in.pressed(kB)) {
    const ParamId id = param_at(strip_, row_);
    if (id != kParamNone) {
      const float def = param_info(id)->default_value;
      if (is_master(strip_)) h.set_project_param(id, def);
      else h.set_track_param(strip_, id, def);
      changed = true;
    }
  }
  if (!is_master(strip_) && strip_ != app_.track() && changed) app_.set_track(strip_);
  // The value under the cursor, in words, in the hint bar.
  if (changed) {
    const ParamId id = param_at(strip_, row_);
    if (id != kParamNone) {
      const float v = is_master(strip_) ? p.mix.get(id) : p.track_param(strip_, id);
      app_.message(std::string(param_info(id)->name) + ": " + format_param(id, v));
    }
  }
  if (changed || in.pressed(~0u)) app_.invalidate(app_.content_rect());
}

void MixerView::tick() {
  // Meters: rise at once, fall back slowly.
  const int n = int(app_.project().tracks.size());
  meters_.resize(size_t(n), 0.0f);
  const Layout l = layout();
  for (int t = 0; t < n; ++t) {
    const float peak = app_.engine().take_track_peak(t);
    const float shown = std::max(peak, meters_[size_t(t)] * 0.88f);
    if (std::fabs(shown - meters_[size_t(t)]) > 0.002f) {
      meters_[size_t(t)] = shown;
      const Rect s = strip_rect(l, t);
      app_.invalidate(Rect{s.x, l.top + l.name_h, s.w, l.level_h});
    }
  }
  // The master compressor's gain reduction, to a tenth of a dB.
  const float gr = app_.project().mix.comp_threshold > 0
                       ? std::round(app_.engine().compressor_reduction_db() * 10) / 10
                       : -1;
  if (gr != comp_shown_) {
    comp_shown_ = gr;
    const Rect s = strip_rect(l, n);
    app_.invalidate(Rect{s.x, l.top + l.name_h + l.level_h + (kReverb - 1) * l.row_h, s.w, l.row_h});
  }
  const float mp = app_.engine().take_master_peak();
  const float shown = std::max(mp, master_meter_ * 0.88f);
  if (std::fabs(shown - master_meter_) > 0.002f) {
    master_meter_ = shown;
    const Rect s = strip_rect(l, n);
    app_.invalidate(Rect{s.x, l.top + l.name_h, s.w, l.level_h});
  }
}

std::vector<Hint> MixerView::hints() const {
  const InputTracker& in = app_.input();
  if (in.held(kA)) return {{"D-pad", "change"}, {"R+", "bigger steps"}};
  if (!is_master(strip_) && (row_ == kMute || row_ == kSolo)) return {{"A", "switch"}, {"X", "effects"}};
  return {{"A+D-pad", "change"}, {"B", "reset"}, {"X", "effects"}, {"Start", "play"}};
}

void MixerView::draw(Canvas& c) {
  const Theme& th = app_.theme();
  const Project& p = app_.project();
  const Layout l = layout();
  const Rect content = app_.content_rect();
  const Font& fb = Font::get(FontFace::SemiBold, app_.u(11));
  const Font& small = Font::get(FontFace::Regular, app_.u(10));
  c.fill(content, th.background);

  // Row labels.
  c.fill(l.labels, th.header);
  auto row_y = [&](int row) { return row == kLevel ? l.top + l.name_h : l.top + l.name_h + l.level_h + (row - 1) * l.row_h; };
  auto row_h = [&](int row) { return row == kLevel ? l.level_h : l.row_h; };
  for (int row = 0; row < kNumRows; ++row) {
    const int y = row_y(row);
    c.text(small, l.labels.x + app_.u(6), y + (row_h(row) + small.ascent() - small.descent()) / 2, kRowNames[row],
           th.text_secondary);
  }
  c.vline(l.labels.right() - 1, content.y, content.bottom(), th.divider);

  const int n = strips();
  meters_.resize(p.tracks.size(), 0.0f);
  for (int s = 0; s < n; ++s) {
    const Rect sr = strip_rect(l, s);
    const bool master = is_master(s);
    const Color col = master ? th.text : app_.track_colour(s);
    const int pad = int(app_.u(4));
    if (s % 2) c.fill(sr, th.row_black);
    // Name.
    c.fill(Rect{sr.x + pad, l.top, sr.w - 2 * pad, int(app_.u(3))}, col);
    // A name too wide for a narrow strip gets a smaller size first; only then is it cut short.
    std::string name = master ? "Master" : p.tracks[size_t(s)].name;
    const Font* nf = &fb;
    for (float size = 10; size >= 8.5f && nf->measure(name.c_str()) > sr.w - pad; size -= 1.5f)
      nf = &Font::get(FontFace::SemiBold, app_.u(size));
    while (name.size() > 1 && nf->measure(name.c_str()) > sr.w - pad) name.pop_back();
    c.text(*nf, sr.x + (sr.w - nf->measure(name.c_str())) / 2, l.top + int(app_.u(20)), name.c_str(), th.text);

    // Level: fader bar with the meter beside it.
    const float lvl = master ? p.mix.master / 1.5f : p.tracks[size_t(s)].level;
    const float meter = master ? master_meter_ : meters_[size_t(s)];
    const int ly = row_y(kLevel) + int(app_.u(4)), lh = row_h(kLevel) - int(app_.u(22));
    const int fw = std::max(6, int(app_.u(10))), mw = std::max(3, int(app_.u(4)));
    const int fx = sr.x + sr.w / 2 - fw, mx = sr.x + sr.w / 2 + int(app_.u(3));
    c.fill_rounded(Rect{fx, ly, fw, lh}, app_.u(2), th.grid_beat);
    const int fh = int(lh * std::min(1.0f, lvl));
    c.fill_rounded(Rect{fx, ly + lh - fh, fw, fh}, app_.u(2), col);
    c.fill(Rect{mx, ly, mw, lh}, th.grid_step);
    const int mh = int(lh * std::min(1.0f, meter));
    c.fill(Rect{mx, ly + lh - mh, mw, mh}, meter > 0.98f ? th.cursor : th.text_secondary);
    char b[16];
    std::snprintf(b, sizeof b, "%.0f", lvl * (master ? 150 : 100));
    c.text(small, sr.x + (sr.w - small.measure(b)) / 2, ly + lh + int(app_.u(14)), b, th.text_secondary);

    // Small rows.
    for (int row = kPan; row < kNumRows; ++row) {
      const int y = row_y(row);
      const Rect cell{sr.x + pad, y + int(app_.u(4)), sr.w - 2 * pad, l.row_h - int(app_.u(8))};
      if (master && row > kDelay) continue;
      if (!master && (row == kMute || row == kSolo)) {
        const Track& t = p.tracks[size_t(s)];
        const bool on = row == kMute ? t.mute : t.solo;
        c.fill_rounded(cell, app_.u(3), on ? (row == kSolo ? th.cursor : th.text) : th.chip);
        const char* txt = row == kMute ? "M" : "S";
        c.text(fb, cell.x + (cell.w - fb.measure(txt)) / 2, cell.y + (cell.h + fb.ascent() - fb.descent()) / 2, txt,
               on ? th.header : th.text_secondary);
        continue;
      }
      const ParamId id = param_at(s, row);
      const ParamInfo* info = param_info(id);
      const float v = master ? p.mix.get(id) : p.track_param(s, id);
      // The master's rows have a label above the bar, so its bar sits at the bottom of the cell.
      const int bar_h = std::max(3, int(app_.u(4)));
      const Rect bar{cell.x, master ? cell.bottom() - bar_h : cell.y + cell.h / 2 - int(app_.u(2)), cell.w, bar_h};
      c.fill(bar, th.grid_beat);
      if (info->min < 0) {  // centred (pan, EQ)
        const int mid = bar.x + bar.w / 2;
        const int x = bar.x + int((v - info->min) / (info->max - info->min) * bar.w);
        c.fill(Rect{std::min(mid, x), bar.y, std::abs(x - mid) + 1, bar.h}, col);
        c.vline(mid, bar.y - int(app_.u(2)), bar.bottom() + int(app_.u(2)), th.grid_bar);
      } else {
        c.fill(Rect{bar.x, bar.y, int(bar.w * (v - info->min) / (info->max - info->min)), bar.h}, col);
      }
    }
    if (master) {
      for (int row = kPan; row <= kDelay; ++row)
        c.text(small, sr.x + pad, row_y(row) + int(app_.u(3)) + small.ascent(), kMasterRowNames[row], th.text_secondary);
      // The compressor (set in the effects panel): how hard it's working.
      c.text(small, sr.x + pad, row_y(kReverb) + int(app_.u(9)), "Comp", th.text_secondary);
      char gr[24];
      if (comp_shown_ < 0) std::snprintf(gr, sizeof gr, "Off");
      else std::snprintf(gr, sizeof gr, "-%.1f dB", comp_shown_);
      c.text(fb, sr.x + pad, row_y(kReverb) + int(app_.u(24)), gr, comp_shown_ >= 6 ? th.warning : th.text);
    }
  }

  // Cursor.
  {
    const Rect sr = strip_rect(l, strip_);
    const Rect cur{sr.x + 1, row_y(row_), sr.w - 2, row_h(row_)};
    c.stroke_rect(cur, std::max(2, int(app_.u(1.5f))), th.cursor);
  }
}

}  // namespace twill
