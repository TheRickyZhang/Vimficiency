#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "Optimizer/Result.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/Mode.h"
#include "Utils/NeovimOracle.h"
#include "Utils/OracleReplay.h"

namespace OptimizerResultChecks {

inline void expectTopResultsReplay(
    NeovimOracle& oracle,
    const std::vector<Result>& results,
    const Lines& initial,
    CursorPos initialPos,
    const Lines& goal,
    size_t maxResults,
    std::string_view context,
    std::optional<CursorPos> goalPos = std::nullopt,
    std::optional<Mode> goalMode = Mode::Normal) {
  if (results.empty()) {
    ADD_FAILURE() << "No results" << (context.empty() ? "" : " for ")
                  << context;
    return;
  }

  size_t limit = std::min(maxResults, results.size());
  for (size_t i = 0; i < limit; i++) {
    std::string resultContext(context);
    if (!resultContext.empty()) resultContext += " ";
    resultContext += "result " + std::to_string(i);
    EXPECT_TRUE(OracleReplay::matches(
        oracle, initial, initialPos, results[i].getSequence().str(),
        goal, goalPos, goalMode, resultContext));
  }
}

}  // namespace OptimizerResultChecks
