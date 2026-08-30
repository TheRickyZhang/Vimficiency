#pragma once

#include <optional>

#include "Boundary/NavBoundary.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizerParams.h"
#include "Optimizer/NavOptimizer/BufferIndex.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Types/CharInterval.h"
#include "Types/CharRange.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/NavContext.h"

struct CompositionRangeMotionSearch {
  Lines subset;
  int beginLine;
  int endLine;
  CursorPos localCursor;
  CursorPos localRangeBegin;
  CursorPos localRangeEnd;
  CharInterval motionRange;
  NavBoundary subsetBoundary;
  BufferIndex bufferIndex;
  int bufferIndexLineOffset;

  CursorPos toBufferPos(CursorPos local) const {
    local.line += beginLine;
    return local;
  }
};

struct CompositionBufferIndexWindow {
  BufferIndex index;
  int lineOffset;
};

CompositionBufferIndexWindow buildCompositionBufferIndexWindow(
    const Lines& lines, int navBeginLine, int navEndLine);

std::optional<CompositionRangeMotionSearch> buildCompositionRangeMotionSearch(
    const Lines& lines,
    CursorPos cursor,
    int targetLine,
    int beginCol,
    int endCol,
    const NavBoundary& boundary,
    const CompositionOptimizerParams& params);

LandingNavResult optimizeCompositionRangeMotion(
    NavOptimizer& navOptimizer,
    const CompositionRangeMotionSearch& search,
    const NavContext& navContext,
    const CompositionOptimizerParams& params,
    int maxResults,
    const SearchControl* control = nullptr);
