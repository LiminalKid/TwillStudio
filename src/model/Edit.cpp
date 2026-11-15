#include "model/Edit.h"

#include <algorithm>

namespace twill {

bool EditHistory::apply(const Edit& e) {
  switch (e.kind) {
    case Edit::Kind::AddNote: {
      Clip* c = p_.find_clip(e.clip_id);
      if (!c || c->find(e.note.id)) return false;
      c->notes.push_back(e.note);
      c->next_note_id = std::max(c->next_note_id, e.note.id + 1);
      c->sort();
      return true;
    }
    case Edit::Kind::RemoveNote: {
      Clip* c = p_.find_clip(e.clip_id);
      if (!c) return false;
      auto it = std::find_if(c->notes.begin(), c->notes.end(),
                             [&](const Note& n) { return n.id == e.note.id; });
      if (it == c->notes.end()) return false;
      c->notes.erase(it);
      return true;
    }
    case Edit::Kind::ChangeNote: {
      Clip* c = p_.find_clip(e.clip_id);
      Note* n = c ? c->find(e.note_after.id) : nullptr;
      if (!n) return false;
      *n = e.note_after;
      c->sort();
      return true;
    }
    case Edit::Kind::SetTrackParam:
      if (e.track < 0 || e.track >= int(p_.tracks.size())) return false;
      p_.set_track_param(e.track, e.param, e.after);
      return true;
    case Edit::Kind::SetTempo:
      p_.tempo = e.after;
      return true;
    case Edit::Kind::AddClip:
      if (p_.find_clip(e.clip.id)) return false;
      p_.clips.push_back(e.clip);
      p_.next_clip_id = std::max(p_.next_clip_id, e.clip.id + 1);
      return true;
    case Edit::Kind::RemoveClip: {
      auto it = std::find_if(p_.clips.begin(), p_.clips.end(),
                             [&](const Clip& c) { return c.id == e.clip.id; });
      if (it == p_.clips.end()) return false;
      p_.clips.erase(it);
      return true;
    }
    case Edit::Kind::ChangeClip: {
      Clip* c = p_.find_clip(e.clip_after.id);
      if (!c) return false;
      *c = e.clip_after;
      return true;
    }
    case Edit::Kind::SetArrangement:
      if (e.track < 0 || e.track >= int(p_.tracks.size())) return false;
      p_.tracks[e.track].arrangement = e.placements_after;
      return true;
    case Edit::Kind::SetKeyScale: {
      int v = int(e.after);
      p_.key = v % 12;
      p_.scale = ScaleType(v / 12);
      return true;
    }
    case Edit::Kind::SetProjectParam:
      p_.mix.set(e.param, e.after);
      return true;
    case Edit::Kind::MoveTrack: {
      const int n = int(p_.tracks.size()), from = int(e.before), to = int(e.after);
      if (from < 0 || from >= n || to < 0 || to >= n) return false;
      Track t = std::move(p_.tracks[size_t(from)]);
      p_.tracks.erase(p_.tracks.begin() + from);
      p_.tracks.insert(p_.tracks.begin() + to, std::move(t));
      return true;
    }
    case Edit::Kind::ChangeTrack: {
      if (e.track < 0 || e.track >= int(p_.tracks.size())) return false;
      Track t = e.track_after;
      t.arrangement = p_.tracks[size_t(e.track)].arrangement;
      p_.tracks[size_t(e.track)] = std::move(t);
      return true;
    }
    case Edit::Kind::SetTrackFlags: {
      if (e.track < 0 || e.track >= int(p_.tracks.size())) return false;
      int v = int(e.after);
      Track& t = p_.tracks[e.track];
      t.mute = v & 1;
      t.solo = (v >> 1) & 1;
      t.voices = v >> 2;
      return true;
    }
  }
  return false;
}

Edit EditHistory::inverse(const Edit& e) {
  Edit inv = e;
  switch (e.kind) {
    case Edit::Kind::AddNote: inv.kind = Edit::Kind::RemoveNote; break;
    case Edit::Kind::RemoveNote: inv.kind = Edit::Kind::AddNote; break;
    case Edit::Kind::ChangeNote:
      inv.note = e.note_after;
      inv.note_after = e.note;
      break;
    case Edit::Kind::SetTrackParam:
    case Edit::Kind::SetTempo:
    case Edit::Kind::SetKeyScale:
    case Edit::Kind::SetTrackFlags:
    case Edit::Kind::SetProjectParam:
    case Edit::Kind::MoveTrack:
      inv.before = e.after;
      inv.after = e.before;
      break;
    case Edit::Kind::AddClip: inv.kind = Edit::Kind::RemoveClip; break;
    case Edit::Kind::RemoveClip: inv.kind = Edit::Kind::AddClip; break;
    case Edit::Kind::ChangeClip:
      inv.clip = e.clip_after;
      inv.clip_after = e.clip;
      break;
    case Edit::Kind::SetArrangement:
      inv.placements = e.placements_after;
      inv.placements_after = e.placements;
      break;
    case Edit::Kind::ChangeTrack:
      inv.track_before = e.track_after;
      inv.track_after = e.track_before;
      break;
  }
  return inv;
}

void EditHistory::record(const Edit& e) {
  redo_.clear();
  ++revision_;
  if (group_depth_ > 0) {
    if (!open_group_started_) {
      undo_.emplace_back();
      open_group_started_ = true;
    }
    undo_.back().push_back(e);
    last_mergeable_ = false;
    return;
  }
  bool mergeable = e.kind == Edit::Kind::SetTrackParam || e.kind == Edit::Kind::SetTempo ||
                   e.kind == Edit::Kind::SetProjectParam;
  if (mergeable && last_mergeable_ && !undo_.empty() && undo_.back().size() == 1) {
    Edit& last = undo_.back()[0];
    if (last.kind == e.kind && last.track == e.track && last.param == e.param) {
      last.after = e.after;
      return;
    }
  }
  undo_.push_back(Group{e});
  last_mergeable_ = mergeable;
}

uint32_t EditHistory::add_note(uint32_t clip_id, Note note) {
  Clip* c = p_.find_clip(clip_id);
  if (!c) return 0;
  note.id = c->next_note_id;
  Edit e = Edit::of(Edit::Kind::AddNote);
  e.clip_id = clip_id;
  e.note = note;
  if (!apply(e)) return 0;
  record(e);
  return note.id;
}

bool EditHistory::remove_note(uint32_t clip_id, uint32_t note_id) {
  Clip* c = p_.find_clip(clip_id);
  const Note* n = c ? c->find(note_id) : nullptr;
  if (!n) return false;
  Edit e = Edit::of(Edit::Kind::RemoveNote);
  e.clip_id = clip_id;
  e.note = *n;
  apply(e);
  record(e);
  return true;
}

bool EditHistory::change_note(uint32_t clip_id, const Note& updated) {
  Clip* c = p_.find_clip(clip_id);
  const Note* n = c ? c->find(updated.id) : nullptr;
  if (!n) return false;
  if (*n == updated) return true;
  Edit e = Edit::of(Edit::Kind::ChangeNote);
  e.clip_id = clip_id;
  e.note = *n;
  e.note_after = updated;
  apply(e);
  record(e);
  return true;
}

void EditHistory::set_track_param(int track, ParamId id, float value) {
  if (track < 0 || track >= int(p_.tracks.size())) return;
  Edit e = Edit::of(Edit::Kind::SetTrackParam);
  e.track = track;
  e.param = id;
  e.before = p_.track_param(track, id);
  Edit change = e;
  change.after = value;
  apply(change);
  e.after = p_.track_param(track, id);  // after clamping
  if (e.after == e.before) return;
  record(e);
}

void EditHistory::set_tempo(float bpm) {
  bpm = std::min(300.0f, std::max(20.0f, bpm));
  if (bpm == p_.tempo) return;
  Edit e = Edit::of(Edit::Kind::SetTempo);
  e.before = p_.tempo;
  e.after = bpm;
  apply(e);
  record(e);
}

void EditHistory::set_project_param(ParamId id, float value) {
  Edit e = Edit::of(Edit::Kind::SetProjectParam);
  e.param = id;
  e.before = p_.mix.get(id);
  MixSettings probe = p_.mix;
  probe.set(id, value);
  e.after = probe.get(id);  // after clamping
  if (e.after == e.before) return;
  apply(e);
  record(e);
}

void EditHistory::set_key_scale(int key, ScaleType scale) {
  Edit e = Edit::of(Edit::Kind::SetKeyScale);
  e.before = float(p_.key + 12 * int(p_.scale));
  e.after = float(((key % 12) + 12) % 12 + 12 * int(scale));
  if (e.before == e.after) return;
  apply(e);
  record(e);
}

void EditHistory::set_track_flags(int track, bool mute, bool solo, int voices) {
  if (track < 0 || track >= int(p_.tracks.size())) return;
  const Track& t = p_.tracks[track];
  voices = std::max(0, std::min(voices, kMaxVoicesPerTrack));  // 0 is legato
  Edit e = Edit::of(Edit::Kind::SetTrackFlags);
  e.track = track;
  e.before = float(int(t.mute) | int(t.solo) << 1 | t.voices << 2);
  e.after = float(int(mute) | int(solo) << 1 | voices << 2);
  if (e.before == e.after) return;
  apply(e);
  record(e);
}

void EditHistory::change_track(int track, const Track& updated) {
  if (track < 0 || track >= int(p_.tracks.size())) return;
  Track before = p_.tracks[size_t(track)];
  Track after = updated;
  after.arrangement = before.arrangement;
  if (after == before) return;
  before.arrangement.clear();  // not needed in the undo record
  after.arrangement.clear();
  Edit e = Edit::of(Edit::Kind::ChangeTrack);
  e.track = track;
  e.track_before = std::move(before);
  e.track_after = std::move(after);
  apply(e);
  record(e);
}

uint32_t EditHistory::add_clip(int track, Tick length, Tick place_at) {
  if (track < 0 || track >= int(p_.tracks.size())) return 0;
  Edit add = Edit::of(Edit::Kind::AddClip);
  add.clip.id = p_.next_clip_id;
  add.clip.track = track;
  add.clip.length = std::max<Tick>(1, length);
  std::vector<Placement> placements = p_.tracks[track].arrangement;
  placements.push_back(Placement{place_at, add.clip.id, 0, add.clip.length});
  std::sort(placements.begin(), placements.end(),
            [](const Placement& a, const Placement& b) { return a.start < b.start; });
  begin_group();
  apply(add);
  record(add);
  set_arrangement(track, placements);
  end_group();
  return add.clip.id;
}

bool EditHistory::change_clip(const Clip& updated) {
  Clip* c = p_.find_clip(updated.id);
  if (!c) return false;
  if (*c == updated) return true;
  Edit e = Edit::of(Edit::Kind::ChangeClip);
  e.clip = *c;
  e.clip_after = updated;
  apply(e);
  record(e);
  return true;
}

void EditHistory::set_arrangement(int track, std::vector<Placement> placements) {
  if (track < 0 || track >= int(p_.tracks.size())) return;
  std::sort(placements.begin(), placements.end(),
            [](const Placement& a, const Placement& b) { return a.start < b.start; });
  if (placements == p_.tracks[track].arrangement) return;
  Edit e = Edit::of(Edit::Kind::SetArrangement);
  e.track = track;
  e.placements = p_.tracks[track].arrangement;
  e.placements_after = std::move(placements);
  apply(e);
  record(e);
}

void EditHistory::move_track(int from, int to) {
  const int n = int(p_.tracks.size());
  if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
  Edit e = Edit::of(Edit::Kind::MoveTrack);
  e.before = float(from);
  e.after = float(to);
  apply(e);
  record(e);
}

void EditHistory::clear_track(int track) {
  if (track < 0 || track >= int(p_.tracks.size())) return;
  Track fresh;
  fresh.name = "Track " + std::to_string(track + 1);
  fresh.colour = p_.tracks[size_t(track)].colour;
  begin_group();
  set_arrangement(track, {});
  change_track(track, fresh);
  end_group();
}

void EditHistory::begin_group() {
  if (group_depth_++ == 0) open_group_started_ = false;
}

void EditHistory::end_group() {
  if (group_depth_ > 0) --group_depth_;
}

bool EditHistory::undo() {
  if (undo_.empty()) return false;
  Group g = std::move(undo_.back());
  undo_.pop_back();
  for (auto it = g.rbegin(); it != g.rend(); ++it) apply(inverse(*it));
  redo_.push_back(std::move(g));
  last_mergeable_ = false;
  ++revision_;
  return true;
}

bool EditHistory::redo() {
  if (redo_.empty()) return false;
  Group g = std::move(redo_.back());
  redo_.pop_back();
  for (const Edit& e : g) apply(e);
  undo_.push_back(std::move(g));
  last_mergeable_ = false;
  ++revision_;
  return true;
}

}  // namespace twill
