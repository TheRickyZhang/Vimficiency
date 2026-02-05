#pragma once

#include <vector>
#include <string>

#include "Optimizer/Config.h"
#include "Optimizer/Result.h"
#include "CompositionOptimizerParams.h"
#include "Boundary/MotionBoundary.h"
#include "Editor/NavContext.h"
#include "Editor/Position.h"
#include "Keyboard/MotionToKeys.h"
#include "Utils/Lines.h"

struct CompositionOptimizer {
  Config config;

  explicit CompositionOptimizer(const Config& config) : config(config) {}

  // Composes edit transitions + movement. Pre-computes edit regions, then searches for optimal sequence.
  // Much slower; ~ O(n^2) + Sigma (m_i)^3, higher constant factor.
  std::vector<Result> optimize(
    // Core information
    const Lines& initialLines,
    const Position initialPos,
    const Lines& goalLines,
    const Position goalPos,

    // Search tuning
    CompositionOptimizerParams params = {},
    const std::string& userSequence = "",

    // Continuation from broader context
    const MotionBoundary& boundary = MotionBoundary(),

    // Niche settings
    const NavContext& navigationContext = NavContext(),
    const MotionToKeys& rawMotionToKeys = EXPLORABLE_MOTIONS
  );
};
