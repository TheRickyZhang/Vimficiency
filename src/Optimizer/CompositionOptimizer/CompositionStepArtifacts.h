#pragma once

#include <optional>

#include "CompositionOptimizerParams.h"
#include "CompositionSearchContext.h"
#include "DiffState.h"
#include "JoinPlan.h"
#include "Keyboard/Config.h"
#include "Optimizer/EditOptimizer/EditOptimizer.h"
#include "Types/Lines.h"

EditResult computeEditResultForDiff(
    const DiffState& diff,
    const CompositionOptimizerParams& params,
    const Config& config,
    int* nodesExplored = nullptr);

BracketQuoteContext computeTextObjectContextForDiff(
    const DiffState& diff,
    const Lines& preEditLines);

std::optional<JoinPlan> computeJoinPlanForDiff(
    const DiffState& diff,
    const Lines& preEditLines,
    const CompositionOptimizerParams& params,
    const Config& config,
    int* nodesExplored = nullptr);
