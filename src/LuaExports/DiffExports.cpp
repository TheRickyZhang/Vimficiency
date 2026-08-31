#include "LuaExports/Common.h"
#include "Optimizer/DiffPlanner/VimDiff.h"
#include "Optimizer/DiffPlanner/DiffAlgorithm.h"
#include "Optimizer/DiffPlanner/DiffState.h"
#include "Optimizer/DiffPlanner/MyersDiff.h"

using namespace std;
namespace helpers = VF::LuaExports::helpers;
namespace payload = VF::LuaExports::payload;
using VF::LuaExports::g_config_internal;

namespace {

// Standalone planner run for results that lack stored diffs (files saved
// before the analyze payload carried them). Live sessions take their regions
// from the analyze payload — the partition the search actually planned
// against; this run uses default cost options, so it can differ from a tuned
// optimizer config.
VF::LuaExports::Result<string> computeDiffsImpl(
    const char *encoded_initial_lines,
    size_t encoded_initial_lines_len,
    const char *encoded_goal_lines,
    size_t encoded_goal_lines_len,
    int diff_algorithm) {
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
    vector<VimDiff::Plan> plans = VimDiff::calculate(initialLines, goalLines, g_config_internal);
    if (!plans.empty()) diffs = std::move(plans.front().diffs);
  } else if (diff_algorithm == DiffAlgorithm::MyersDiff) {
    diffs = MyersDiff::calculate(initialLines, goalLines);
  } else {
    diffs = {};
  }
  return payload::encodeDiffRegions(diffs);
}

}  // namespace

extern "C" {

VFByteSlice vf_compute_diffs(
    const char *encoded_initial_lines,
    size_t encoded_initial_lines_len,
    const char *encoded_goal_lines,
    size_t encoded_goal_lines_len,
    int diff_algorithm) {
  static string result_storage;
  return helpers::storeBytes(
      result_storage,
      computeDiffsImpl(
          encoded_initial_lines,
          encoded_initial_lines_len,
          encoded_goal_lines,
          encoded_goal_lines_len,
          diff_algorithm));
}

}
