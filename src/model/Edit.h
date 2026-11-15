// Edits to a Project, as reversible commands with undo and redo (design §6, "Threads and edits").
// The UI changes the project only through EditHistory; each change bumps revision(), which tells
// the app to build a new snapshot for the engine.
#pragma once
#include <cstdint>
#include <vector>

#include "model/Project.h"

namespace twill {

struct Edit {
  enum class Kind : uint8_t {
    AddNote, RemoveNote, ChangeNote, SetTrackParam, SetTempo,
    AddClip, RemoveClip, ChangeClip,  // clip: `clip` (ChangeClip: before), `clip_after`
    SetArrangement,                   // track's placements: `placements` before, `placements_after`
    SetKeyScale,                      // before/after = key + 12 * scale
    SetTrackFlags,                    // before/after = mute | solo << 1 | voices << 2
    ChangeTrack,                      // `track_before`, `track_after`, all but the arrangement
    SetProjectParam,                  // `param`, before/after (master and effects)
    MoveTrack,                        // the track at `before` moves to `after`, the rest close up
  };
  static Edit of(Kind k) {
    Edit e;
    e.kind = k;
    return e;
  }
  Kind kind = Kind::AddNote;
  uint32_t clip_id = 0;
  Note note;        // AddNote / RemoveNote: the note; ChangeNote: the note before
  Note note_after;  // ChangeNote: the note after
  int track = 0;
  ParamId param = kParamNone;
  float before = 0, after = 0;  // SetTrackParam, SetTempo, SetKeyScale, SetTrackFlags
  Clip clip, clip_after;
  std::vector<Placement> placements, placements_after;
  Track track_before, track_after;
};

class EditHistory {
 public:
  explicit EditHistory(Project& project) : p_(project) {}

  // Each returns false (and changes nothing) if the clip or note doesn't exist.
  uint32_t add_note(uint32_t clip_id, Note note);  // returns the new note's id, or 0
  bool remove_note(uint32_t clip_id, uint32_t note_id);
  bool change_note(uint32_t clip_id, const Note& updated);  // matched by updated.id
  // Consecutive changes to the same parameter of the same track merge into one undo step, so
  // turning a knob is one step.
  void set_track_param(int track, ParamId id, float value);
  void set_tempo(float bpm);
  // Master and effects settings; consecutive changes to one merge like set_track_param.
  void set_project_param(ParamId id, float value);
  void set_key_scale(int key, ScaleType scale);
  void set_track_flags(int track, bool mute, bool solo, int voices);
  // Replaces a track's settings (instrument, samples, names...); its arrangement is kept.
  void change_track(int track, const Track& updated);
  // Moves a track, with its clips and settings, to another position; the tracks between shift
  // over by one.
  void move_track(int from, int to);
  // Empties a track: no clips, default sound and settings, named for its position. Keeps its
  // colour. One undo step.
  void clear_track(int track);
  // Arrangement.
  uint32_t add_clip(int track, Tick length, Tick place_at);  // a new clip, placed; returns its id
  bool change_clip(const Clip& updated);                      // replaces the clip with the same id
  void set_arrangement(int track, std::vector<Placement> placements);

  // Edits between begin_group() and end_group() undo together (moving several notes at once).
  void begin_group();
  void end_group();

  bool can_undo() const { return !undo_.empty(); }
  bool can_redo() const { return !redo_.empty(); }
  bool undo();
  // Forgets all undo and redo steps (after loading or starting a project).
  void clear() {
    undo_.clear();
    redo_.clear();
    group_depth_ = 0;
    last_mergeable_ = false;
    ++revision_;
  }
  bool redo();

  uint64_t revision() const { return revision_; }  // changes on every edit, undo and redo

 private:
  using Group = std::vector<Edit>;
  bool apply(const Edit& e);
  static Edit inverse(const Edit& e);
  void record(const Edit& e);

  Project& p_;
  std::vector<Group> undo_, redo_;
  int group_depth_ = 0;
  bool open_group_started_ = false;
  bool last_mergeable_ = false;  // the last undo step is a single parameter change
  uint64_t revision_ = 0;
};

}  // namespace twill
