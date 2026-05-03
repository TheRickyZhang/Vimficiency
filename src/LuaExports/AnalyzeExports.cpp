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
using VF::LuaExports::g_config_internal;
using VF::LuaExports::kEventFieldSep;

namespace {

Lines splitLines(string_view text) {
  Lines lines;
  istringstream stream{string(text)};
  string line;
  while (getline(stream, line)) {
    lines.push_back(line);
  }
  return lines;
}

VF::LuaExports::Result<string> analyzeImpl(
    const char *initial_text,
    const char *goal_text,
    int boundaryFirstCol,
    int boundaryLastCol,
    bool hasLinesAbove,
    bool hasLinesBelow,
    int start_row,
    int start_col,
    int end_row,
    int end_col,
    const char *keyseq,
    int window_height,
    int scroll_amount,
    int results_calculated,
    const char *optimizer_overrides) {
  CHECK(results_calculated >= 0, "results_calculated must be non-negative");
  auto initialText = helpers::requiredText(initial_text, "initial_text");
  if (!initialText) return unexpected(initialText.error());
  auto goalText = helpers::requiredText(goal_text, "goal_text");
  if (!goalText) return unexpected(goalText.error());
  auto keyseqTextResult = helpers::requiredText(keyseq, "keyseq");
  if (!keyseqTextResult) return unexpected(keyseqTextResult.error());

  const auto overrides = OptimizerParamOverrides::parse(
      helpers::optionalText(optimizer_overrides));

  const Lines initialLines = splitLines(*initialText);
  const Lines goalLines = splitLines(*goalText);
  const string keyseqText(*keyseqTextResult);

  assert(!initialLines.empty() && "FFI contract: buffer must have at least one line");
  assert(!goalLines.empty() && "FFI contract: goal buffer must have at least one line");

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
      // Field separator is the shared `kEventFieldSep` (0x1F Unit Sep);
      // rationale and the project-wide convention live in
      // `dev/lua/ffi-separators.md`.
      oss << result->getSequence().view()
          << kEventFieldSep
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

const char *vf_analyze(
    const char *initial_text,
    const char *goal_text,
    int boundaryFirstCol,
    int boundaryLastCol,
    bool hasLinesAbove,
    bool hasLinesBelow,
    int start_row,
    int start_col,
    int end_row,
    int end_col,
    const char *keyseq,
    int window_height,
    int scroll_amount,
    int results_calculated,
    const char *optimizer_overrides) {
  static string result_storage;
  return helpers::storeString(
      result_storage,
      analyzeImpl(
          initial_text,
          goal_text,
          boundaryFirstCol,
          boundaryLastCol,
          hasLinesAbove,
          hasLinesBelow,
          start_row,
          start_col,
          end_row,
          end_col,
          keyseq,
          window_height,
          scroll_amount,
          results_calculated,
          optimizer_overrides));
}

}
