#include "LuaExports/Shared.h"
#include "Explore/Explore.h"

#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

using namespace std;

namespace {

using Explore::View;
using Explore::Recommendation;
using Explore::State;
using Explore::Outcome;
using vimficiency::lua_exports::ExportError;
using vimficiency::lua_exports::ExportErrorKind;
using vimficiency::lua_exports::Result;
using vimficiency::lua_exports::export_helpers::optionalText;
using vimficiency::lua_exports::export_helpers::requiredText;
using vimficiency::lua_exports::payload::decodeLineArray;

// Explore views keyed by opaque ID. Lua owns the policy deciding whether
// a given captured session's Explore view is reused or respawned; C++ only
// requires that IDs are unique per live view.
unordered_map<int, View> g_views;
int g_next_id = 0;

Result<View*> requireView(int view_id) {
  auto it = g_views.find(view_id);
  if (it == g_views.end()) {
    return unexpected(ExportError{
        .kind = ExportErrorKind::InvalidValue,
        .message = "interactive explore view not found",
    });
  }
  return &it->second;
}

string encodeField(string_view field) {
  return to_string(field.size()) + ":" + string(field);
}

template <typename... Fields>
string encodeFields(const Fields&... fields) {
  string out;
  ((out += encodeField(fields)), ...);
  return out;
}

string intField(int v) { return to_string(v); }
string doubleField(double v) {
  ostringstream oss;
  oss.setf(ios::fixed);
  oss.precision(6);
  oss << v;
  return oss.str();
}

// State payload. Length-prefixed fields so raw bytes in acceptedSeq survive
// round-tripping. The trailing four ints carry the current target range
// (-1 sentinels when not in ApproachEdit or no plan).
string encodeState(const View& v) {
  int tbRow = -1, tbCol = -1, teRow = -1, teCol = -1;
  if (auto range = v.currentTargetRange()) {
    tbRow = range->first.line;
    tbCol = range->first.col;
    teRow = range->second.line;
    teCol = range->second.col;
  }
  const Explore::State& st = v.state();
  return encodeFields(
      string_view(to_string(st.step.kind)),
      string_view(intField(st.step.editIndex)),
      string_view(st.step.remainingText),
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

// Apply-result payload. Two statuses only — "Applied" and "Rejected". View
// state (phase, cursor, seq, cost) is echoed inline so Lua can treat the
// apply response as a state refresh in one call. `crossed` = crossed an edit
// boundary (useful for Lua's decide-to-rewrite-scratch decision).
string encodeOutcome(const View& v, const Outcome& outcome) {
  const char* status = outcome.has_value() ? "Applied" : "Rejected";
  const int crossed = (outcome.has_value() && outcome.value().crossedEditBoundary) ? 1 : 0;
  const string& reason = outcome.has_value() ? string() : outcome.error().reason;
  const Explore::State& st = v.state();
  return encodeFields(
      string_view(status),
      string_view(to_string(st.step.kind)),
      string_view(intField(st.step.editIndex)),
      string_view(st.step.remainingText),
      string_view(intField(st.cursor.line)),
      string_view(intField(st.cursor.col)),
      string_view(st.acceptedSeq),
      string_view(doubleField(st.acceptedCost)),
      string_view(reason),
      string_view(intField(crossed)));
}

// 7 fields — `typedText` is the insert-mode tail the user must type after
// `text`, empty for motions and normal-mode-only edits. Lua's
// parse_explore_recommendations must match the field count.
string encodeRecommendation(const Recommendation& rec) {
  return encodeFields(
      string_view(rec.text),
      string_view(rec.kind),
      string_view(doubleField(rec.cost)),
      string_view(doubleField(rec.totalPathCost)),
      string_view(intField(rec.landingRow)),
      string_view(intField(rec.landingCol)),
      string_view(rec.typedText));
}

string encodeRecommendations(const vector<Recommendation>& recs) {
  string out = encodeField(intField(static_cast<int>(recs.size())));
  for (const auto& r : recs) out += encodeRecommendation(r);
  return out;
}

Result<string> startImpl(
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
  auto initialTextRes = requiredText(encoded_initial_lines, "encoded_initial_lines");
  if (!initialTextRes) return unexpected(initialTextRes.error());
  auto goalTextRes = requiredText(encoded_goal_lines, "encoded_goal_lines");
  if (!goalTextRes) return unexpected(goalTextRes.error());

  auto initialLinesRes = decodeLineArray(*initialTextRes);
  if (!initialLinesRes) return unexpected(initialLinesRes.error());
  auto goalLinesRes = decodeLineArray(*goalTextRes);
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
  const string_view userSeq = optionalText(user_seq);

  const int view_id = ++g_next_id;
  g_views.try_emplace(
      view_id,
      std::move(initialLines),
      initialPos,
      std::move(goalLines),
      goalPos,
      std::move(boundary),
      navContext,
      vimficiency::lua_exports::g_config_internal,
      userSeq);
  return to_string(view_id);
}

}  // namespace

namespace Explore {
// Make `to_string(Phase)` reachable via ADL inside our anonymous namespace's
// `encodeState` / `encodeOutcome`. The declaration is in Explore.h.
}

extern "C" {

const char* vimficiency_explore_start(
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
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return startImpl(
        encoded_initial_lines, start_row, start_col,
        encoded_goal_lines, end_row, end_col,
        boundary_first_col, boundary_last_col,
        has_lines_above, has_lines_below,
        window_height, scroll_amount,
        user_seq);
  });
}

int vimficiency_explore_destroy(int view_id) {
  return static_cast<int>(g_views.erase(view_id));
}

const char* vimficiency_explore_state(int view_id) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireView(view_id).transform(
        [](View* v) { return encodeState(*v); });
  });
}

const char* vimficiency_explore_recommendations(
    int view_id,
    int max_count,
    bool allow_multiple_motions_per_position,
    bool allow_multiple_edits_per_position) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireView(view_id).transform(
        [&](View* v) {
          return encodeRecommendations(v->recommendations(
              max_count,
              allow_multiple_motions_per_position,
              allow_multiple_edits_per_position));
        });
  });
}

const char* vimficiency_explore_apply_motion(int view_id, const char* motion_text) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireView(view_id).and_then(
        [&](View* v) -> vimficiency::lua_exports::Result<string> {
          return requiredText(motion_text, "motion_text").transform(
              [&](string_view text) {
                auto outcome = v->applyMotion(text);
                return encodeOutcome(*v, outcome);
              });
        });
  });
}

const char* vimficiency_explore_accept_cursor_move(
    int view_id, int new_row, int new_col, const char* raw_keys) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireView(view_id).transform(
        [&](View* v) {
          auto outcome = v->acceptCursorMove(CursorPos(new_row, new_col), optionalText(raw_keys));
          return encodeOutcome(*v, outcome);
        });
  });
}

const char* vimficiency_explore_accept_buffer_state(
    int view_id,
    const char* encoded_lines,
    int new_row,
    int new_col,
    const char* raw_keys) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireView(view_id).and_then(
        [&](View* v) -> vimficiency::lua_exports::Result<string> {
          return requiredText(encoded_lines, "encoded_lines")
              .and_then(decodeLineArray)
              .transform([&](const Lines& newLines) {
                auto outcome = v->acceptBufferState(
                    newLines, CursorPos(new_row, new_col), optionalText(raw_keys));
                return encodeOutcome(*v, outcome);
              });
        });
  });
}

const char* vimficiency_explore_apply_edit(int view_id, const char* text) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireView(view_id).and_then(
        [&](View* v) -> vimficiency::lua_exports::Result<string> {
          return requiredText(text, "text").transform(
              [&](string_view edit_text) {
                auto outcome = v->applyEdit(edit_text);
                return encodeOutcome(*v, outcome);
              });
        });
  });
}

const char* vimficiency_explore_current_lines(int view_id) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireView(view_id).transform(
        [](View* v) {
          string out;
          for (const auto& line : v->state().lines) {
            out += to_string(line.size()) + ":" + string(line);
          }
          return out;
        });
  });
}

const char* vimficiency_explore_begin_edit(int view_id, bool enters_insert_mode,
                                           const char* required_typed_text) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireView(view_id).transform(
        [&](View* v) {
          auto outcome = v->beginEdit(enters_insert_mode, optionalText(required_typed_text));
          return encodeOutcome(*v, outcome);
        });
  });
}

const char* vimficiency_explore_insert_text(int view_id, const char* typed_chunk) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireView(view_id).and_then(
        [&](View* v) -> vimficiency::lua_exports::Result<string> {
          return requiredText(typed_chunk, "typed_chunk").transform(
              [&](string_view chunk) {
                auto outcome = v->consumeInsertText(chunk);
                return encodeOutcome(*v, outcome);
              });
        });
  });
}

const char* vimficiency_explore_exit_insert(int view_id) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireView(view_id).transform(
        [](View* v) {
          auto outcome = v->exitInsertMode();
          return encodeOutcome(*v, outcome);
        });
  });
}

const char* vimficiency_explore_accept_insert_exit(
    int view_id,
    const char* encoded_lines,
    int new_row,
    int new_col,
    const char* raw_keys) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireView(view_id).and_then(
        [&](View* v) -> vimficiency::lua_exports::Result<string> {
          return requiredText(encoded_lines, "encoded_lines")
              .and_then(decodeLineArray)
              .transform([&](const Lines& newLines) {
                auto outcome = v->acceptInsertExit(
                    newLines, CursorPos(new_row, new_col), optionalText(raw_keys));
                return encodeOutcome(*v, outcome);
              });
        });
  });
}

const char* vimficiency_explore_cancel_pending_insert(int view_id) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireView(view_id).transform(
        [](View* v) {
          auto outcome = v->cancelPendingInsert();
          return encodeOutcome(*v, outcome);
        });
  });
}

const char* vimficiency_explore_undo(int view_id) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireView(view_id).transform(
        [](View* v) {
          auto outcome = v->undo();
          return encodeOutcome(*v, outcome);
        });
  });
}

const char* vimficiency_explore_redo(int view_id) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireView(view_id).transform(
        [](View* v) {
          auto outcome = v->redo();
          return encodeOutcome(*v, outcome);
        });
  });
}

}  // extern "C"
