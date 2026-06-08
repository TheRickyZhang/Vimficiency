#include "LuaExports/Common.h"
#include "Optimizer/DiffPlanner/VimDiff.h"
#include "Optimizer/DiffPlanner/DiffAlgorithm.h"
#include "Optimizer/DiffPlanner/DiffState.h"
#include "Optimizer/DiffPlanner/MyersDiff.h"
#include <sstream>

using namespace std;
namespace helpers = VF::LuaExports::helpers;
namespace payload = VF::LuaExports::payload;
using VF::LuaExports::EVENT_FIELD_SEP;
using VF::LuaExports::g_config_internal;

namespace {

// Surface the optimizer's own character-level diff so a viewer can highlight
// exactly which columns changed. For each contiguous change region we emit the
// initial-side (deleted) span and the goal-side (inserted) span. Both spans
// share `beginPos` — everything before the first differing character is the
// common prefix, identical in both buffers — but we serialize both explicitly
// so the consumer treats each pane independently.
//
// Wire format: one record per diff, fields separated by EVENT_FIELD_SEP,
// records terminated by '\n':
//   init_begin_row, init_begin_col, init_end_row, init_end_col,
//   goal_begin_row, goal_begin_col, goal_end_row, goal_end_col
// All positions are 0-indexed; spans are half-open [begin, end). An empty
// initial span (begin == end) is a pure insertion; an empty goal span is a
// pure deletion.
VF::LuaExports::Result<string> computeDiffsImpl(
    const char *encoded_initial_lines,
    size_t encoded_initial_lines_len,
    const char *encoded_goal_lines,
    size_t encoded_goal_lines_len,
    int diff_algorithm,
    double diff_open_penalty) {
  auto initialText = helpers::requiredBytes(
      encoded_initial_lines, encoded_initial_lines_len, "encoded_initial_lines");
  if (!initialText) return unexpected(initialText.error());
  auto goalText = helpers::requiredBytes(
      encoded_goal_lines, encoded_goal_lines_len, "encoded_goal_lines");
  if (!goalText) return unexpected(goalText.error());

  auto initialLinesResult = payload::decodeLineArray(*initialText);
  if (!initialLinesResult) return unexpected(initialLinesResult.error());
  auto goalLinesResult = payload::decodeLineArray(*goalText);
  if (!goalLinesResult) return unexpected(goalLinesResult.error());
  const Lines initialLines = std::move(*initialLinesResult);
  const Lines goalLines = std::move(*goalLinesResult);

  // Match the algorithm the CompositionOptimizer used so the view's highlight
  // is the same breakdown (see CompositionSearchContext.cpp).
  vector<DiffState> diffs;
  if (diff_algorithm == DiffAlgorithm::VimDiff) {
    vector<VimDiff::Plan> plans = VimDiff::calculate(
        initialLines, goalLines, g_config_internal,
        VimDiff::CostOptions{.diffOpenPenalty = diff_open_penalty});
    if (!plans.empty()) diffs = std::move(plans.front().diffs);
  } else if (diff_algorithm == DiffAlgorithm::MyersDiff) {
    diffs = MyersDiff::calculate(initialLines, goalLines);
  } else {
    diffs = {};
  }

  ostringstream oss;
  for (const DiffState &d : diffs) {
    const CursorPos goalEnd =
        DiffText::advancePositionByText(d.beginPos, d.insertedText);
    oss << d.beginPos.line << EVENT_FIELD_SEP << d.beginPos.col << EVENT_FIELD_SEP
        << d.endPos.line << EVENT_FIELD_SEP << d.endPos.col << EVENT_FIELD_SEP
        << d.beginPos.line << EVENT_FIELD_SEP << d.beginPos.col << EVENT_FIELD_SEP
        << goalEnd.line << EVENT_FIELD_SEP << goalEnd.col << "\n";
  }
  return oss.str();
}

}  // namespace

extern "C" {

VFByteSlice vf_compute_diffs(
    const char *encoded_initial_lines,
    size_t encoded_initial_lines_len,
    const char *encoded_goal_lines,
    size_t encoded_goal_lines_len,
    int diff_algorithm,
    double diff_open_penalty) {
  static string result_storage;
  return helpers::storeBytes(
      result_storage,
      computeDiffsImpl(
          encoded_initial_lines,
          encoded_initial_lines_len,
          encoded_goal_lines,
          encoded_goal_lines_len,
          diff_algorithm,
          diff_open_penalty));
}

}
