#include "LuaExports/Common.h"
#include "Boundary/NavBoundary.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Optimizer/OptimizerParamOverrides.h"
#include "Utils/Debug.h"
#include <algorithm>
#include <iomanip>

using namespace std;
namespace helpers = VF::LuaExports::helpers;
namespace payload = VF::LuaExports::payload;
using VF::LuaExports::g_config_internal;
using VF::LuaExports::EVENT_FIELD_SEP;

namespace {

VF::LuaExports::Result<string> analyzeImpl(
    const char *encoded_initial_lines,
    size_t encoded_initial_lines_len,
    const char *encoded_goal_lines,
    size_t encoded_goal_lines_len,
    int boundaryFirstCol,
    int boundaryLastCol,
    bool hasLinesAbove,
    bool hasLinesBelow,
    int start_row,
    int start_col,
    int end_row,
    int end_col,
    const char *keyseq,
    size_t keyseq_len,
    int window_height,
    int scroll_amount,
    int results_calculated,
    const char *optimizer_overrides,
    size_t optimizer_overrides_len) {
  CHECK(results_calculated >= 0, "results_calculated must be non-negative");
  auto initialText = helpers::requiredBytes(
      encoded_initial_lines,
      encoded_initial_lines_len,
      "encoded_initial_lines");
  if (!initialText) return unexpected(initialText.error());
  auto goalText = helpers::requiredBytes(
      encoded_goal_lines,
      encoded_goal_lines_len,
      "encoded_goal_lines");
  if (!goalText) return unexpected(goalText.error());
  auto keyseqTextResult = helpers::requiredBytes(keyseq, keyseq_len, "keyseq");
  if (!keyseqTextResult) return unexpected(keyseqTextResult.error());
  auto optimizerOverridesText = helpers::requiredBytes(
      optimizer_overrides,
      optimizer_overrides_len,
      "optimizer_overrides");
  if (!optimizerOverridesText) return unexpected(optimizerOverridesText.error());

  const auto overrides = OptimizerParamOverrides::parse(
      *optimizerOverridesText);

  auto initialLinesResult = payload::decodeLineArray(*initialText);
  if (!initialLinesResult) return unexpected(initialLinesResult.error());
  auto goalLinesResult = payload::decodeLineArray(*goalText);
  if (!goalLinesResult) return unexpected(goalLinesResult.error());
  const Lines initialLines = std::move(*initialLinesResult);
  const Lines goalLines = std::move(*goalLinesResult);
  const string keyseqText(*keyseqTextResult);

  const CursorPos initialPos(start_row, start_col);
  const CursorPos goalPos(end_row, end_col);
  const NavContext navigationContext(window_height, scroll_amount);

  vector<::Result> results;
  NavBoundary boundary(
      initialLines,
      CursorPos(0, boundaryFirstCol),
      CursorPos(static_cast<int>(initialLines.size()) - 1, boundaryLastCol + 1),
      hasLinesAbove,
      hasLinesBelow);

  if (initialLines == goalLines) {
    // NavOptimizer returns LandingResult (with goalPos); for analyze we only
    // surface (sequence, cost), so down-convert to Result for shared output.
    NavOptimizer opt(g_config_internal);
    NavOptimizerParams navParams = NavOptimizerParams{}
        .withMaxResults(results_calculated)
        .withMaxResultsPerEndPos(2);
    overrides.applyTo(navParams);
    auto navResults = opt.optimize(
        initialLines,
        initialPos,
        goalPos,
        navParams,
        keyseqText,
        boundary,
        navigationContext).getResults();
    results.reserve(navResults.size());
    for (const auto& r : navResults) {
      results.emplace_back(r.getSequence().str(), r.getCost());
    }
  } else {
    CompositionOptimizer opt(g_config_internal);
    CompositionOptimizerParams compParams =
        CompositionOptimizerParams{}.withMaxResults(results_calculated);
    overrides.applyTo(compParams);
    results = opt.optimize(
        initialLines,
        initialPos,
        goalLines,
        goalPos,
        compParams,
        keyseqText,
        boundary,
        navigationContext).getResults();
  }

  const double userCost = getEffort(keyseqText, g_config_internal);
  vector<const ::Result*> validResults;
  for (const ::Result& result : results) {
    if (result.getSequence().empty()) continue;
    validResults.push_back(&result);
  }

  sort(validResults.begin(), validResults.end(),
            [](const ::Result* a, const ::Result* b) { return a->getCost() < b->getCost(); });

  ostringstream oss;
  if (validResults.empty()) {
    oss << "no results";
  } else {
    oss << "size: " << validResults.size() << " user_cost: "
        << fixed << setprecision(3) << userCost << "\n";
    for (const ::Result* result : validResults) {
      // Machine-readable export for the Lua bridge: use the raw sequence
      // bytes, not Sequence's human formatter, so insert-mode text and
      // <Esc> survive round-tripping through ffi.lua's line parser.
      // Field separator is the shared `EVENT_FIELD_SEP` (0x1F Unit Sep);
      // rationale and the project-wide convention live in
      // `dev/lua/ffi-separators.md`.
      oss << result->getSequence().view()
          << EVENT_FIELD_SEP
          << fixed << setprecision(3) << result->getCost() << "\n";
    }
  }

  if constexpr (DEBUG_ENABLED) {
    oss << "\n ----------------DEBUG---------------- \n" << consume_debug_output();
  }
  return oss.str();
}

}  // namespace

extern "C" {

VFByteSlice vf_analyze(
    const char *encoded_initial_lines,
    size_t encoded_initial_lines_len,
    const char *encoded_goal_lines,
    size_t encoded_goal_lines_len,
    int boundaryFirstCol,
    int boundaryLastCol,
    bool hasLinesAbove,
    bool hasLinesBelow,
    int start_row,
    int start_col,
    int end_row,
    int end_col,
    const char *keyseq,
    size_t keyseq_len,
    int window_height,
    int scroll_amount,
    int results_calculated,
    const char *optimizer_overrides,
    size_t optimizer_overrides_len) {
  static string result_storage;
  return helpers::storeBytes(
      result_storage,
      analyzeImpl(
          encoded_initial_lines,
          encoded_initial_lines_len,
          encoded_goal_lines,
          encoded_goal_lines_len,
          boundaryFirstCol,
          boundaryLastCol,
          hasLinesAbove,
          hasLinesBelow,
          start_row,
          start_col,
          end_row,
          end_col,
          keyseq,
          keyseq_len,
          window_height,
          scroll_amount,
          results_calculated,
          optimizer_overrides,
          optimizer_overrides_len));
}

}
