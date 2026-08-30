#include "CompositionMotionSearch.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "Optimizer/CompositionOptimizer/CompositionNavParams.h"

using namespace std;

CompositionBufferIndexWindow buildCompositionBufferIndexWindow(
    const Lines& lines, int navBeginLine, int navEndLine) {
  assert(navBeginLine >= 0);
  assert(navBeginLine < navEndLine);
  assert(navEndLine <= static_cast<int>(lines.size()));

  int indexBeginLine = max(0, navBeginLine - BUFFER_INDEX_CONTEXT_LINES);
  int indexEndLine = min(
      static_cast<int>(lines.size()), navEndLine + BUFFER_INDEX_CONTEXT_LINES);
  return CompositionBufferIndexWindow{
      BufferIndex(lines.getLineRange(indexBeginLine, indexEndLine)),
      navBeginLine - indexBeginLine,
  };
}

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
  auto bufferIndexWindow = buildCompositionBufferIndexWindow(lines, beginLine, endLine);

  return CompositionRangeMotionSearch{
      std::move(subset),
      beginLine,
      endLine,
      localCursor,
      localRangeBegin,
      localRangeEnd,
      motionRange,
      std::move(subsetBoundary),
      std::move(bufferIndexWindow.index),
      bufferIndexWindow.lineOffset,
  };
}

LandingNavResult optimizeCompositionRangeMotion(
    NavOptimizer& navOptimizer,
    const CompositionRangeMotionSearch& search,
    const NavContext& navContext,
    const CompositionOptimizerParams& params,
    int maxResults,
    const SearchControl* control) {
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
      search.bufferIndex,
      search.bufferIndexLineOffset,
      control);
}
