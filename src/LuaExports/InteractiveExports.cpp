#include "LuaExports/Common.h"
#include "LuaExports/ViewRegistry.h"
#include "Explore/Explore.h"

#include <string>
#include <string_view>

using namespace std;
namespace helpers = VF::LuaExports::helpers;
namespace payload = VF::LuaExports::payload;

namespace {

using Explore::View;
using Explore::Suggestion;
using Explore::Outcome;
using VF::LuaExports::ViewRegistry;

ViewRegistry g_registry;

using payload::encodeField;
using payload::encodeFields;
using payload::intField;
using payload::doubleField;

struct PhaseWireFields {
  string_view kind;
  int editIndex;
};
PhaseWireFields phaseWireFields(const Explore::Phase& phase) {
  return {
      Explore::phaseKindName(phase),
      Explore::phaseIndex(phase),
  };
}

// State payload. Length-prefixed fields so raw bytes in acceptedSeq survive
// round-tripping. `is_completed` is the C++-side derived predicate (cursor
// has reached goalPos at the post-final-edit nav segment). The trailing
// four ints carry the current target range (-1 sentinels in Insert phase,
// where the UI doesn't render a range).
string encodeState(const View& v) {
  int tbRow = -1, tbCol = -1, teRow = -1, teCol = -1;
  if (auto range = v.currentTargetRange()) {
    tbRow = range->first.line;
    tbCol = range->first.col;
    teRow = range->second.line;
    teCol = range->second.col;
  }
  const Explore::State& st = v.state();
  const PhaseWireFields phase = phaseWireFields(st.phase);
  return encodeFields(
      phase.kind,
      string_view(intField(phase.editIndex)),
      string_view(intField(v.isCompleted() ? 1 : 0)),
      string_view(intField(st.cursor.line)),
      string_view(intField(st.cursor.col)),
      string_view(intField(v.totalEdits())),
      string_view(doubleField(st.acceptedCost)),
      string_view(st.acceptedSeq),
      string_view(intField(st.acceptedRevision)),
      string_view(intField(v.canUndo() ? 1 : 0)),
      string_view(intField(v.canRedo() ? 1 : 0)),
      string_view(intField(tbRow)),
      string_view(intField(tbCol)),
      string_view(intField(teRow)),
      string_view(intField(teCol)));
}

// Apply-result payload. Two fields: status ("Applied" | "Rejected") and
// reason (empty on Applied). The new view state is read separately via
// vf_explore_state — Lua callers refresh state after every action anyway.
string encodeOutcome(const Outcome& outcome) {
  const char* status = outcome.has_value() ? "Applied" : "Rejected";
  const string& reason = outcome.has_value() ? string() : outcome.error().reason;
  return encodeFields(string_view(status), string_view(reason));
}

// Wire encoder for one recommendation. 4 fields — Lua's
// parse_explore_recommendations must match the field count. The recommendation's
// "kind" (motion vs edit-structural vs typed-text) is implicit in the view's
// current phase, which Lua already reads off `state.phase.kind`.
string encodeSuggestion(const Suggestion& item) {
  return encodeFields(
      string_view(item.token),
      string_view(doubleField(item.costDiff)),
      string_view(intField(item.landingPos.line)),
      string_view(intField(item.landingPos.col)));
}

string encodeSuggestions(const vector<Suggestion>& recs) {
  string out = encodeField(intField(static_cast<int>(recs.size())));
  for (const auto& r : recs) out += encodeSuggestion(r);
  return out;
}

VF::LuaExports::Result<string> startImpl(
    const char* encoded_initial_lines,
    int start_row,
    int start_col,
    const char* encoded_goal_lines,
    int end_row,
    int end_col,
    int boundary_first_col,
    int boundary_last_col,
    bool has_lines_above,
    bool has_lines_below,
    int window_height,
    int scroll_amount,
    const char* user_seq) {
  auto initialTextRes = helpers::requiredText(encoded_initial_lines, "encoded_initial_lines");
  if (!initialTextRes) return unexpected(initialTextRes.error());
  auto goalTextRes = helpers::requiredText(encoded_goal_lines, "encoded_goal_lines");
  if (!goalTextRes) return unexpected(goalTextRes.error());

  auto initialLinesRes = payload::decodeLineArray(*initialTextRes);
  if (!initialLinesRes) return unexpected(initialLinesRes.error());
  auto goalLinesRes = payload::decodeLineArray(*goalTextRes);
  if (!goalLinesRes) return unexpected(goalLinesRes.error());

  Lines initialLines = std::move(*initialLinesRes);
  Lines goalLines = std::move(*goalLinesRes);
  if (initialLines.empty()) initialLines.push_back(Line(""));
  if (goalLines.empty()) goalLines.push_back(Line(""));

  const CursorPos initialPos(start_row, start_col);
  const CursorPos goalPos(end_row, end_col);

  NavBoundary boundary(
      initialLines,
      CursorPos(0, boundary_first_col),
      CursorPos(static_cast<int>(initialLines.size()) - 1, boundary_last_col + 1),
      has_lines_above,
      has_lines_below);

  NavContext navContext(window_height, scroll_amount);
  const string_view userSeq = helpers::optionalText(user_seq);

  const int view_id = g_registry.create(
      std::move(initialLines),
      initialPos,
      std::move(goalLines),
      goalPos,
      std::move(boundary),
      navContext,
      VF::LuaExports::g_config_internal,
      userSeq);
  return to_string(view_id);
}

}  // namespace

extern "C" {

const char* vf_explore_start(
    const char* encoded_initial_lines,
    int start_row,
    int start_col,
    const char* encoded_goal_lines,
    int end_row,
    int end_col,
    int boundary_first_col,
    int boundary_last_col,
    bool has_lines_above,
    bool has_lines_below,
    int window_height,
    int scroll_amount,
    const char* user_seq) {
  static string storage;
  return helpers::storeString(storage, startImpl(
      encoded_initial_lines, start_row, start_col,
      encoded_goal_lines, end_row, end_col,
      boundary_first_col, boundary_last_col,
      has_lines_above, has_lines_below,
      window_height, scroll_amount,
      user_seq));
}

int vf_explore_destroy(int view_id) {
  return g_registry.destroy(view_id) ? 1 : 0;
}

const char* vf_explore_state(int view_id) {
  static string storage;
  storage = encodeState(g_registry.get(view_id));
  return storage.c_str();
}

const char* vf_explore_recommendations(
    int view_id,
    int max_count,
    int nav_max_results_per_end_pos,
    int transform_max_results_per_start_pos) {
  CHECK(max_count >= 0, "max_count must be non-negative");
  static string storage;
  View& v = g_registry.get(view_id);
  storage = encodeSuggestions(v.recommendations(
      max_count,
      nav_max_results_per_end_pos,
      transform_max_results_per_start_pos));
  return storage.c_str();
}

const char* vf_explore_apply_movement(int view_id, const char* movement_text) {
  static string storage;
  View& v = g_registry.get(view_id);
  return helpers::storeString(storage, helpers::requiredText(movement_text, "movement_text").transform(
      [&](string_view text) {
        return encodeOutcome(v.applyMovement(text));
      }));
}

const char* vf_explore_accept_cursor_move(
    int view_id, int new_row, int new_col, const char* raw_keys) {
  static string storage;
  View& v = g_registry.get(view_id);
  storage = encodeOutcome(
      v.acceptCursorMove(CursorPos(new_row, new_col), helpers::optionalText(raw_keys)));
  return storage.c_str();
}

const char* vf_explore_accept_buffer_state(
    int view_id,
    const char* encoded_lines,
    int new_row,
    int new_col,
    const char* raw_keys) {
  static string storage;
  View& v = g_registry.get(view_id);
  return helpers::storeString(storage, helpers::requiredText(encoded_lines, "encoded_lines")
      .and_then(payload::decodeLineArray)
      .transform([&](const Lines& newLines) {
        return encodeOutcome(
            v.acceptBufferState(newLines, CursorPos(new_row, new_col), helpers::optionalText(raw_keys)));
      }));
}

const char* vf_explore_apply_edit(int view_id, const char* text) {
  static string storage;
  View& v = g_registry.get(view_id);
  return helpers::storeString(storage, helpers::requiredText(text, "text").transform(
      [&](string_view edit_text) {
        return encodeOutcome(v.applyEdit(edit_text));
      }));
}

const char* vf_explore_current_lines(int view_id) {
  static string storage;
  View& v = g_registry.get(view_id);
  storage.clear();
  for (const auto& line : v.state().lines) {
    storage += to_string(line.size()) + ":" + string(line);
  }
  return storage.c_str();
}

const char* vf_explore_begin_insert(int view_id) {
  static string storage;
  View& v = g_registry.get(view_id);
  storage = encodeOutcome(v.beginInsert());
  return storage.c_str();
}

const char* vf_explore_accept_insert_exit(
    int view_id,
    const char* encoded_lines,
    int new_row,
    int new_col,
    const char* raw_keys) {
  static string storage;
  View& v = g_registry.get(view_id);
  return helpers::storeString(storage, helpers::requiredText(encoded_lines, "encoded_lines")
      .and_then(payload::decodeLineArray)
      .transform([&](const Lines& newLines) {
        return encodeOutcome(
            v.acceptInsertExit(newLines, CursorPos(new_row, new_col), helpers::optionalText(raw_keys)));
      }));
}

const char* vf_explore_cancel_insert(int view_id) {
  static string storage;
  View& v = g_registry.get(view_id);
  storage = encodeOutcome(v.cancelInsert());
  return storage.c_str();
}

const char* vf_explore_undo(int view_id) {
  static string storage;
  View& v = g_registry.get(view_id);
  storage = encodeOutcome(v.undo());
  return storage.c_str();
}

const char* vf_explore_redo(int view_id) {
  static string storage;
  View& v = g_registry.get(view_id);
  storage = encodeOutcome(v.redo());
  return storage.c_str();
}

}  // extern "C"
