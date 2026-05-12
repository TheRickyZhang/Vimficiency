#include "CompositionMotionSearch.h"

#include <algorithm>
#include <utility>

#include "Optimizer/CompositionOptimizer/CompositionNavParams.h"

using namespace std;

optional<CompositionRangeMotionSearch> buildCompositionRangeMotionSearch(
    const Lines& lines,
    CursorPos cursor,
    int targetLine,
    int beginCol,
    int endCol,
    const NavBoundary& boundary,
    const CompositionOptimizerParams& params) {
  if (targetLine < 0 || beginCol < 0 || endCol <= beginCol) return nullopt;
  if (targetLine >= static_cast<int>(lines.size())) return nullopt;

  auto [beginLine, endLine] = lines.minmaxBoundWithPadding(
      min(cursor.line, targetLine), max(cursor.line, targetLine) + 1,
      params.navPaddingAbove, params.navPaddingBelow);
  Lines subset = lines.getLineRange(beginLine, endLine);

  CursorPos localCursor(cursor.line - beginLine, cursor.col, cursor.targetCol);
  CursorPos localRangeBegin(targetLine - beginLine, beginCol);
  CursorPos localRangeEnd(targetLine - beginLine, endCol);
  CharInterval motionRange(CharRange(localRangeBegin, localRangeEnd), subset);

  CursorPos subsetFirst(0, 0);
  CursorPos subsetEnd(static_cast<int>(subset.size()) - 1,
      subset.back().effectiveSize());
  NavBoundary subsetBoundary(subset, subsetFirst, subsetEnd,
      beginLine > 0 || boundary.hasLinesAbove(),
      endLine <= lines.lastLine() || boundary.hasLinesBelow());

  return CompositionRangeMotionSearch{
      std::move(subset),
      beginLine,
      endLine,
      localCursor,
      localRangeBegin,
      localRangeEnd,
      motionRange,
      std::move(subsetBoundary),
  };
}

LandingNavResult optimizeCompositionRangeMotion(
    NavOptimizer& navOptimizer,
    const CompositionRangeMotionSearch& search,
    const NavContext& navContext,
    const CompositionOptimizerParams& params,
    int maxResults) {
  auto navParams = navParamsForCompositionMotion(params)
      .withMaxResults(maxResults);
  return navOptimizer.optimize(
      search.subset,
      search.localCursor,
      search.motionRange,
      navParams,
      "",
      search.subsetBoundary,
      navContext);
}

LandingNavResult optimizeCompositionRangeMotion(
    NavOptimizer& navOptimizer,
    const CompositionRangeMotionSearch& search,
    const NavContext& navContext,
    const CompositionOptimizerParams& params,
    int maxResults,
    const BufferIndex& bufferIndex,
    int lineOffset) {
  auto navParams = navParamsForCompositionMotion(params)
      .withMaxResults(maxResults);
  return navOptimizer.optimize(
      search.subset,
      search.localCursor,
      search.motionRange,
      navParams,
      "",
      search.subsetBoundary,
      navContext,
      bufferIndex,
      lineOffset);
}
