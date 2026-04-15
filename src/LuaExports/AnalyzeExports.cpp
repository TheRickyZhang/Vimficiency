#include "LuaExports/Shared.h"
#include "Boundary/MotionBoundary.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"
#include "Utils/Debug.h"
#include <algorithm>
#include <iomanip>

using namespace std;

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

vimficiency::lua_exports::Result<string> analyzeImpl(
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
    int results_calculated) {
  auto initialText = vimficiency::lua_exports::export_helpers::requiredText(initial_text, "initial_text");
  if (!initialText) return unexpected(initialText.error());
  auto goalText = vimficiency::lua_exports::export_helpers::requiredText(goal_text, "goal_text");
  if (!goalText) return unexpected(goalText.error());
  auto keyseqTextResult = vimficiency::lua_exports::export_helpers::requiredText(keyseq, "keyseq");
  if (!keyseqTextResult) return unexpected(keyseqTextResult.error());

  const Lines initialLines = splitLines(*initialText);
  const Lines goalLines = splitLines(*goalText);
  const string keyseqText(*keyseqTextResult);

  assert(!initialLines.empty() && "FFI contract: buffer must have at least one line");
  assert(!goalLines.empty() && "FFI contract: goal buffer must have at least one line");

  const CursorPos initialPos(start_row, start_col);
  const CursorPos goalPos(end_row, end_col);
  const NavContext navigationContext(window_height, scroll_amount);

  vector<::Result> results;
  MotionBoundary boundary(
      initialLines,
      CursorPos(0, boundaryFirstCol),
      CursorPos(static_cast<int>(initialLines.size()) - 1, boundaryLastCol + 1),
      hasLinesAbove,
      hasLinesBelow);

  if (initialLines == goalLines) {
    MotionOptimizer opt(vimficiency::lua_exports::g_config_internal);
    results = opt.optimize(
        initialLines,
        initialPos,
        goalPos,
        MotionOptimizerParams{}.withMaxResults(results_calculated),
        keyseqText,
        boundary,
        navigationContext).getResults();
  } else {
    CompositionOptimizer opt(vimficiency::lua_exports::g_config_internal);
    results = opt.optimize(
        initialLines,
        initialPos,
        goalLines,
        goalPos,
        CompositionOptimizerParams{}.withMaxResults(results_calculated),
        keyseqText,
        boundary,
        navigationContext).getResults();
  }

  const double userCost = getEffort(keyseqText, vimficiency::lua_exports::g_config_internal);
  vector<const ::Result*> validResults;
  for (const ::Result& result : results) {
    if (!result.isValid()) continue;
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
      oss << result->getSequence() << " "
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

const char *vimficiency_analyze(
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
    int results_calculated) {
  static string result_storage;
  return vimficiency::lua_exports::export_helpers::storeString(result_storage, [&] {
    return analyzeImpl(
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
        results_calculated);
  });
}

}
