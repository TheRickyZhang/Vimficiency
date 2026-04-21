#include "LuaExports/Shared.h"
#include "Session/Explore.h"

#include <optional>
#include <sstream>
#include <string>
#include <string_view>

using namespace std;

namespace {

using Explore::Session;
using Explore::Phase;
using Explore::Recommendation;
using Explore::State;
using Explore::Rejected;
using Explore::Outcome;
using vimficiency::lua_exports::ExportError;
using vimficiency::lua_exports::ExportErrorKind;
using vimficiency::lua_exports::Result;
using vimficiency::lua_exports::export_helpers::optionalText;
using vimficiency::lua_exports::export_helpers::requiredText;
using vimficiency::lua_exports::payload::decodeLineArray;

optional<Session> g_session;
int g_generation = 0;

Result<Session*> requireSession(int generation) {
  if (!g_session || generation != g_generation) {
    return unexpected(ExportError{
        .kind = ExportErrorKind::InvalidValue,
        .message = "interactive explore session not found",
    });
  }
  return &*g_session;
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
string encodeState(const Session& s) {
  int tbRow = -1, tbCol = -1, teRow = -1, teCol = -1;
  if (auto range = s.currentTargetRange()) {
    tbRow = range->first.line;
    tbCol = range->first.col;
    teRow = range->second.line;
    teCol = range->second.col;
  }
  const Explore::State& st = s.state();
  return encodeFields(
      string_view(to_string(st.step.kind)),
      string_view(intField(st.step.editIndex)),
      string_view(st.step.remainingText),
      string_view(intField(st.cursor.line)),
      string_view(intField(st.cursor.col)),
      string_view(intField(s.totalEdits())),
      string_view(doubleField(st.acceptedCost)),
      string_view(st.acceptedSeq),
      string_view(intField(st.acceptedRevision)),
      string_view(intField(s.canUndo() ? 1 : 0)),
      string_view(intField(s.canRedo() ? 1 : 0)),
      string_view(intField(tbRow)),
      string_view(intField(tbCol)),
      string_view(intField(teRow)),
      string_view(intField(teCol)));
}

// Apply-result payload. Two statuses only — "Applied" and "Rejected". Session
// state (phase, cursor, seq, cost) is echoed inline so Lua can treat the
// apply response as a state refresh in one call. `crossed` = crossed an edit
// boundary (useful for Lua's decide-to-rewrite-scratch decision).
string encodeOutcome(const Session& s, const Outcome& outcome) {
  const char* status = outcome.has_value() ? "Applied" : "Rejected";
  const int crossed = (outcome.has_value() && outcome.value().crossedEditBoundary) ? 1 : 0;
  const string& reason = outcome.has_value() ? string() : outcome.error().reason;
  const Explore::State& st = s.state();
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

string encodeRecommendation(const Recommendation& rec) {
  return encodeFields(
      string_view(rec.text),
      string_view(rec.kind),
      string_view(doubleField(rec.cost)),
      string_view(doubleField(rec.totalPathCost)),
      string_view(intField(rec.landingRow)),
      string_view(intField(rec.landingCol)));
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

  MotionBoundary boundary(
      initialLines,
      CursorPos(0, boundary_first_col),
      CursorPos(static_cast<int>(initialLines.size()) - 1, boundary_last_col + 1),
      has_lines_above,
      has_lines_below);

  NavContext navContext(window_height, scroll_amount);
  const string_view userSeq = optionalText(user_seq);

  g_session.emplace(
      std::move(initialLines),
      initialPos,
      std::move(goalLines),
      goalPos,
      std::move(boundary),
      navContext,
      vimficiency::lua_exports::g_config_internal,
      userSeq);
  ++g_generation;
  return to_string(g_generation);
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

int vimficiency_explore_destroy(int generation) {
  if (!g_session || generation != g_generation) return 0;
  g_session.reset();
  return 1;
}

const char* vimficiency_explore_state(int generation) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireSession(generation).transform(
        [](Session* s) { return encodeState(*s); });
  });
}

const char* vimficiency_explore_recommendations(int generation, int max_count) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireSession(generation).transform(
        [&](Session* s) { return encodeRecommendations(s->recommendations(max_count)); });
  });
}

const char* vimficiency_explore_apply_motion(int generation, const char* motion_text) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireSession(generation).and_then(
        [&](Session* s) -> vimficiency::lua_exports::Result<string> {
          return requiredText(motion_text, "motion_text").transform(
              [&](string_view text) {
                auto outcome = s->applyMotion(text);
                return encodeOutcome(*s, outcome);
              });
        });
  });
}

const char* vimficiency_explore_accept_cursor_move(
    int generation, int new_row, int new_col, const char* raw_keys) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireSession(generation).transform(
        [&](Session* s) {
          auto outcome = s->acceptCursorMove(CursorPos(new_row, new_col), optionalText(raw_keys));
          return encodeOutcome(*s, outcome);
        });
  });
}

const char* vimficiency_explore_accept_buffer_state(
    int generation,
    const char* encoded_lines,
    int new_row,
    int new_col,
    const char* raw_keys) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireSession(generation).and_then(
        [&](Session* s) -> vimficiency::lua_exports::Result<string> {
          return requiredText(encoded_lines, "encoded_lines")
              .and_then(decodeLineArray)
              .transform([&](const Lines& newLines) {
                auto outcome = s->acceptBufferState(
                    newLines, CursorPos(new_row, new_col), optionalText(raw_keys));
                return encodeOutcome(*s, outcome);
              });
        });
  });
}

const char* vimficiency_explore_apply_edit(int generation, const char* text) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireSession(generation).and_then(
        [&](Session* s) -> vimficiency::lua_exports::Result<string> {
          return requiredText(text, "text").transform(
              [&](string_view edit_text) {
                auto outcome = s->applyEdit(edit_text);
                return encodeOutcome(*s, outcome);
              });
        });
  });
}

const char* vimficiency_explore_current_lines(int generation) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireSession(generation).transform(
        [](Session* s) {
          string out;
          for (const auto& line : s->state().lines) {
            out += to_string(line.size()) + ":" + string(line);
          }
          return out;
        });
  });
}

const char* vimficiency_explore_begin_edit(int generation, bool enters_insert_mode,
                                           const char* required_typed_text) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireSession(generation).transform(
        [&](Session* s) {
          auto outcome = s->beginEdit(enters_insert_mode, optionalText(required_typed_text));
          return encodeOutcome(*s, outcome);
        });
  });
}

const char* vimficiency_explore_insert_text(int generation, const char* typed_chunk) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireSession(generation).and_then(
        [&](Session* s) -> vimficiency::lua_exports::Result<string> {
          return requiredText(typed_chunk, "typed_chunk").transform(
              [&](string_view chunk) {
                auto outcome = s->consumeInsertText(chunk);
                return encodeOutcome(*s, outcome);
              });
        });
  });
}

const char* vimficiency_explore_exit_insert(int generation) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireSession(generation).transform(
        [](Session* s) {
          auto outcome = s->exitInsertMode();
          return encodeOutcome(*s, outcome);
        });
  });
}

const char* vimficiency_explore_undo(int generation) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireSession(generation).transform(
        [](Session* s) {
          auto outcome = s->undo();
          return encodeOutcome(*s, outcome);
        });
  });
}

const char* vimficiency_explore_redo(int generation) {
  static string storage;
  return vimficiency::lua_exports::export_helpers::storeString(storage, [&] {
    return requireSession(generation).transform(
        [](Session* s) {
          auto outcome = s->redo();
          return encodeOutcome(*s, outcome);
        });
  });
}

}  // extern "C"
