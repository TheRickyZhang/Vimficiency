#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "Boundary/NavBoundary.h"
#include "Effort/RunningEffort.h"
#include "Explore/Explore.h"
#include "Interpreter/SequenceParser.h"
#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizerParams.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Optimizer/NavOptimizer/NavRangeConversion.h"
#include "Optimizer/OptimizerParamOverrides.h"
#include "Optimizer/Result.h"
#include "Optimizer/TransformOptimizer/TransformFrontier.h"
#include "Optimizer/TransformOptimizer/TransformSequenceDecomposition.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/NavContext.h"

namespace ExploreTestSupport {

class ExploreViewTest : public ::testing::Test {
protected:
  Config config = Config::uniform();
  NavContext navContext{24, 12};

  Explore::View makeView(Lines initial, CursorPos initialPos, Lines goal,
                         CursorPos goalPos) {
    NavBoundary boundary(
        initial, CursorPos(0, 0),
        CursorPos(static_cast<int>(initial.size()) - 1,
                  static_cast<int>(initial.back().size()) + 1),
        /*hasLinesAbove=*/false,
        /*hasLinesBelow=*/false);
    return Explore::View(std::move(initial), initialPos, std::move(goal),
                         goalPos, std::move(boundary), navContext, config);
  }

  Explore::View makeViewWithBoundary(Lines initial, CursorPos initialPos, Lines goal,
                                     CursorPos goalPos, CursorPos boundaryBegin,
                                     CursorPos boundaryEnd) {
    NavBoundary boundary(initial, boundaryBegin, boundaryEnd,
                         /*hasLinesAbove=*/false,
                         /*hasLinesBelow=*/false);
    return Explore::View(std::move(initial), initialPos, std::move(goal),
                         goalPos, std::move(boundary), navContext, config);
  }
};

}  // namespace ExploreTestSupport
