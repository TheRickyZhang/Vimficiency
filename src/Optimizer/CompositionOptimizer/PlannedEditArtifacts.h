#pragma once

#include <optional>

#include "CompositionOptimizerParams.h"
#include "CompositionSearchContext.h"
#include "JoinPlan.h"
#include "Keyboard/Config.h"
#include "Optimizer/DiffPlanner/DiffState.h"
#include "Optimizer/TransformOptimizer/TransformOptimizer.h"
#include "Types/Lines.h"

TransformResult computeTransformResultForDiff(
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
