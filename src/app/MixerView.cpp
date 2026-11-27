#include "app/MixerView.h"
#include "app/Paper.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace twill {

namespace {
constexpr float kLabelW = 64, kNameH = 30, kLevelH = 118, kMinLevelH = 80, kRowH = 28;
constexpr int kRows = 6;  // under the level: pan, three sends, mute, solo
const char* const kRowNames[] = {"Level", "Pan", "Chorus", "Delay", "Reverb", "Mute", "Solo"};
const char* const kMasterRowNames[] = {"Level", "EQ low", "EQ mid", "EQ high", "", "", ""};
const char* const kMasterRowShort[] = {"Level", "Low", "Mid", "High", "", "", ""};  // narrow strips
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
  // Y: hear the track (as in the sound view).
  if (in.pressed(kY) && !in.held(kA) && !is_master(strip_)) app_.listen(strip_);
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
  if (changed) show_value();
  if (changed || in.pressed(~0u)) app_.invalidate(app_.content_rect());
}

void MixerView::show_value() {
  const ParamId id = param_at(strip_, row_);
  if (id == kParamNone) return;
  const Project& p = app_.project();
  const float v = is_master(strip_) ? p.mix.get(id) : p.track_param(strip_, id);
  app_.message(std::string(param_info(id)->name) + ": " + format_param(id, v));
}

int MixerView::row_y(const Layout& l, int row) const {
  return row == kLevel ? l.top + l.name_h : l.top + l.name_h + l.level_h + (row - 1) * l.row_h;
}

int MixerView::row_height(const Layout& l, int row) const { return row == kLevel ? l.level_h : l.row_h; }

Rect MixerView::cell_rect(int strip, int row) const {
  const Layout l = layout();
  const Rect sr = strip_rect(l, strip);
  return Rect{sr.x, row_y(l, row), sr.w, row_height(l, row)};
}

bool MixerView::cell_at(const Layout& l, int x, int y, int& strip, int& row) const {
  if (x < l.x0 || l.strip_w <= 0) return false;
  strip = (x - l.x0) / l.strip_w;
  if (strip >= strips()) return false;
  // The name selects the strip and keeps the row.
  if (y < row_y(l, kLevel)) {
    row = row_;
    return y >= l.top;
  }
  for (int r = 0; r < kNumRows; ++r)
    if (y < row_y(l, r) + row_height(l, r)) {
      row = r;
      return !is_master(strip) || r <= kDelay;
    }
  return false;
}

float MixerView::value_at(const Layout& l, int strip, int row, int x, int y) const {
  // Where the slider is drawn (see draw): the level's bar upright, the rest across.
  const ParamInfo* info = param_info(param_at(strip, row));
  const Rect sr = strip_rect(l, strip);
  float f;
  if (row == kLevel) {
    const int ly = row_y(l, kLevel) + int(app_.u(4)), lh = std::max(1, row_height(l, kLevel) - int(app_.u(22)));
    f = float(ly + lh - y) / float(lh);
  } else {
    const int cx = sr.x + int(app_.u(2.5f)), cw = std::max(1, sr.w - int(app_.u(5)));
    f = float(x - cx) / float(cw);
  }
  f = std::max(0.0f, std::min(1.0f, f));
  float v = info->min + f * (info->max - info->min);
  // Centred sliders catch at the middle.
  if (info->min < 0 && std::fabs(v) < (info->max - info->min) * 0.02f) v = 0;
  return v;
}

void MixerView::set_value(int strip, int row, float v) {
  const ParamId id = param_at(strip, row);
  if (id == kParamNone) return;
  if (is_master(strip)) app_.history().set_project_param(id, v);
  else app_.history().set_track_param(strip, id, v);
}

void MixerView::pointer(const Gesture& g) {
  const Layout l = layout();
  const Rect content = app_.content_rect();
  EditHistory& h = app_.history();
  auto end_group = [&] {
    if (pointer_group_) h.end_group();
    pointer_group_ = false;
  };
  switch (g.kind) {
    case Gesture::Press: {
      sliding_ = false;
      int s, r;
      if (!content.contains(g.x, g.y) || !cell_at(l, g.x, g.y, s, r)) return;
      strip_ = s;
      row_ = r;
      if (!is_master(s) && s != app_.track()) app_.set_track(s);
      const Project& p = app_.project();
      if (!is_master(s) && (r == kMute || r == kSolo)) {  // M and S switch at once
        if (g.button == 1) {
          const Track& t = p.tracks[size_t(s)];
          h.set_track_flags(s, r == kMute ? !t.mute : t.mute, r == kSolo ? !t.solo : t.solo, t.voices);
        }
      } else if (g.y >= row_y(l, kLevel) && param_at(s, r) != kParamNone) {
        if (g.button == 2 || g.taps == 2) {
          set_value(s, r, param_info(param_at(s, r))->default_value);
        } else if (g.button == 1) {
          if (!pointer_group_) h.begin_group();
          pointer_group_ = sliding_ = true;
          set_value(s, r, value_at(l, s, r, g.x, g.y));
        }
        show_value();
      }
      app_.invalidate(content);
      return;
    }
    case Gesture::Drag:
      if (!sliding_) return;
      set_value(strip_, row_, value_at(l, strip_, row_, g.x, g.y));
      show_value();
      app_.invalidate(content);
      return;
    case Gesture::Tap:
    case Gesture::DragEnd:
    case Gesture::Cancel:
      sliding_ = false;
      end_group();
      return;
    case Gesture::Wheel: {
      int s, r;
      if (!content.contains(g.x, g.y) || !cell_at(l, g.x, g.y, s, r) || g.y < row_y(l, kLevel)) return;
      const ParamId id = param_at(s, r);
      if (id == kParamNone) return;
      wheel_acc_ += g.wheel_y + g.wheel_x;
      const int steps = int(wheel_acc_);
      wheel_acc_ -= float(steps);
      if (!steps) return;
      strip_ = s;
      row_ = r;
      if (!is_master(s) && s != app_.track()) app_.set_track(s);
      const ParamInfo* info = param_info(id);
      const float v = is_master(s) ? app_.project().mix.get(id) : app_.project().track_param(s, id);
      set_value(s, r, v + float(steps) * (info->max - info->min) * (g.shift ? 0.1f : 0.02f));
      show_value();
      app_.invalidate(content);
      return;
    }
    case Gesture::LongPress:
    case Gesture::DragStart:
    case Gesture::Pan:
    case Gesture::Pinch:
      return;
  }
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
  if (!is_master(strip_) && (row_ == kMute || row_ == kSolo)) return {{"A", "switch"}, {"Y", "listen"}, {"X", "effects"}};
  if (is_master(strip_)) return {{"A+D-pad", "change"}, {"B", "reset"}, {"X", "effects"}, {"Start", "play"}};
  return {{"A+D-pad", "change"}, {"B", "reset"}, {"Y", "listen"}, {"X", "effects"}, {"Start", "play"}};
}

void MixerView::draw(Canvas& c) {
  const Theme& th = app_.theme();
  const Project& p = app_.project();
  const Layout l = layout();
  const Rect content = app_.content_rect();
  const Font& name_f = Font::get(FontFace::CondensedBold, app_.u(14));
  const Font& label_f = Font::get(FontFace::CondensedSemiBold, app_.u(13));
  const Font& small = Font::get(FontFace::CondensedSemiBold, app_.u(11));
  const Font& value_f = Font::get(FontFace::CondensedBold, app_.u(14));
  const Font& ms_f = Font::get(FontFace::SemiBold, app_.u(13));
  const float sp = app_.u(kCapsSpacing);
  const int line = std::max(1, int(app_.u(1)));
  c.fill(content, th.background);

  // Row labels, on a sheet like the arrangement's track headers.
  c.fill(l.labels, th.panel);
  auto row_y = [&](int row) { return this->row_y(l, row); };
  auto row_h = [&](int row) { return row_height(l, row); };
  for (int row = 0; row < kNumRows; ++row) {
    const int y = row_y(row);
    const std::string name = caps(kRowNames[row]);
    c.text(label_f, l.labels.x + app_.u(8), label_f.baseline_in(y, row_h(row)), name.c_str(), th.text_secondary, sp);
  }
  c.vline(l.labels.right() - 1, content.y, content.bottom(), th.grid_bar);

  const int n = strips();
  meters_.resize(p.tracks.size(), 0.0f);
  for (int s = 0; s < n; ++s) {
    const Rect sr = strip_rect(l, s);
    const bool master = is_master(s);
    const Color col = master ? th.text : app_.track_colour(s);
    const int pad = int(app_.u(4));
    // Name, under a bar of the track's colour. A name too wide for a narrow strip gets a smaller
    // size first; only then is it cut short.
    c.fill(Rect{sr.x + pad, l.top + int(app_.u(4)), sr.w - 2 * pad, int(app_.u(4))}, col);
    std::string name = caps(master ? "Master" : p.tracks[size_t(s)].name);
    const Font* nf = &name_f;
    for (float size = 12; size >= 10 && nf->measure(name.c_str()) > sr.w - pad; size -= 2)
      nf = &Font::get(FontFace::CondensedBold, app_.u(size));
    while (name.size() > 1 && nf->measure(name.c_str()) > sr.w - pad) name.pop_back();
    c.text(*nf, sr.x + (sr.w - nf->measure(name.c_str())) / 2, l.top + l.name_h - int(app_.u(6)), name.c_str(), th.text);

    // Level: a flat bar in the track's colour, with the meter beside it.
    const float lvl = master ? p.mix.master / 1.5f : p.tracks[size_t(s)].level;
    const float meter = master ? master_meter_ : meters_[size_t(s)];
    const int ly = row_y(kLevel) + int(app_.u(4)), lh = row_h(kLevel) - int(app_.u(22));
    const int fw = std::max(6, int(app_.u(9))), mw = std::max(3, int(app_.u(3)));
    const int fx = sr.x + sr.w / 2 - fw + int(app_.u(1)), mx = fx + fw + int(app_.u(3));
    c.fill(Rect{fx, ly, fw, lh}, th.grid_step);
    const int fh = int(lh * std::min(1.0f, lvl));
    c.fill(Rect{fx, ly + lh - fh, fw, fh}, col);
    c.fill(Rect{mx, ly, mw, lh}, th.grid_step);
    const int mh = int(lh * std::min(1.0f, meter));
    c.fill(Rect{mx, ly + lh - mh, mw, mh}, meter > 0.98f ? th.cursor : th.text_secondary);
    char b[16];
    std::snprintf(b, sizeof b, "%.0f", lvl * (master ? 150 : 100));
    c.text(value_f, sr.x + (sr.w - value_f.measure(b)) / 2, ly + lh + int(app_.u(15)), b, th.text);

    // Small rows: sliders (a groove, a fill in the track's colour, a thin thumb), then M and S.
    for (int row = kPan; row < kNumRows; ++row) {
      const int y = row_y(row);
      const Rect cell{sr.x + int(app_.u(2.5f)), y + int(app_.u(4)), sr.w - int(app_.u(5)), l.row_h - int(app_.u(8))};
      if (master && row > kDelay) continue;
      if (!master && (row == kMute || row == kSolo)) {
        const Track& t = p.tracks[size_t(s)];
        const bool on = row == kMute ? t.mute : t.solo;
        const int bw = std::min(int(app_.u(32)), sr.w - int(app_.u(10)));
        const Rect box{sr.x + (sr.w - bw) / 2, y + int(app_.u(3)), bw, l.row_h - int(app_.u(6))};
        const Color box_col = row == kSolo && on ? th.cursor : th.text;
        if (on) c.fill(box, box_col);
        else c.stroke_rect(box, line, th.text);
        const char* txt = row == kMute ? "M" : "S";
        c.text(ms_f, box.x + (box.w - ms_f.measure(txt)) / 2, ms_f.baseline_in(box.y, box.h), txt,
               on ? th.chip_active_text : th.text);
        continue;
      }
      const ParamId id = param_at(s, row);
      const ParamInfo* info = param_info(id);
      const float v = master ? p.mix.get(id) : p.track_param(s, id);
      // The master's rows have a label above the slider, so its slider sits low in the cell.
      const int bar_h = std::max(3, int(app_.u(4)));
      const int bar_y = master ? cell.bottom() - bar_h - int(app_.u(1)) : cell.y + (cell.h - bar_h) / 2;
      const Rect bar{cell.x, bar_y, cell.w, bar_h};
      c.fill(bar, th.grid_step);
      const int x = bar.x + int((v - info->min) / (info->max - info->min) * bar.w);
      if (info->min < 0) {  // centred (pan, EQ): filled from the middle, which is marked
        const int mid = bar.x + bar.w / 2;
        c.fill(Rect{std::min(mid, x), bar.y, std::abs(x - mid) + 1, bar.h}, col);
        c.vline(mid, bar.y - int(app_.u(3)), bar.bottom() + int(app_.u(3)), th.grid_bar);
      } else {
        c.fill(Rect{bar.x, bar.y, x - bar.x, bar.h}, col);
      }
      const int tw = std::max(2, int(app_.u(2)));
      c.fill(Rect{x - tw / 2, bar.y + bar.h / 2 - int(app_.u(4.5f)), tw, int(app_.u(9))}, th.text_secondary);
    }
    if (master) {
      const float fits = sr.w - 2 * pad;
      bool long_names = true;  // all long or all short
      for (int row = kPan; row <= kDelay; ++row)
        long_names = long_names && small.measure(caps(kMasterRowNames[row]).c_str(), sp) <= fits;
      const char* const* names = long_names ? kMasterRowNames : kMasterRowShort;
      for (int row = kPan; row <= kDelay; ++row) {
        const std::string nm = caps(names[row]);
        c.text(small, sr.x + pad, row_y(row) + int(app_.u(3)) + small.cap_height() + int(app_.u(1)), nm.c_str(),
               th.text_secondary, sp);
      }
      // The compressor (set in the effects panel): how hard it's working.
      c.text(small, sr.x + pad, row_y(kReverb) + int(app_.u(10)), "COMP", th.text_secondary, sp);
      char gr[24];
      if (comp_shown_ < 0) std::snprintf(gr, sizeof gr, "OFF");
      else std::snprintf(gr, sizeof gr, "-%.1f dB", comp_shown_);
      if (value_f.measure(gr) > fits && comp_shown_ >= 0) std::snprintf(gr, sizeof gr, "-%.1f", comp_shown_);
      c.text(value_f, sr.x + pad, row_y(kReverb) + int(app_.u(24)), gr, comp_shown_ >= 6 ? th.warning : th.text);
    }
  }

  // The labels lie on the strips like a sheet.
  draw_shadow(c, l.labels.right(), content.y, content.h, int(app_.u(6)), false, th.shadow_strength * 3 / 4);

  // Cursor: viewfinder corners around the selected control.
  {
    const Rect sr = strip_rect(l, strip_);
    const int inset = int(app_.u(3));
    const Rect cur{sr.x + inset, row_y(row_) + int(app_.u(2)), sr.w - 2 * inset, row_h(row_) - int(app_.u(4))};
    draw_brackets(c, cur, th.text, app_.u(1));
  }
}

}  // namespace twill
