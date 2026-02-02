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

  // CompositionOptimizer-specific parameters
  // Overshooting (going past the next edit region) is penalized more heavily
  // than undershooting (not yet reaching it)
  double overshootPenalty = 3.0;
  // Slight bias towards forward (natural left->right, top->bottom order)
  double forwardBias = 2.0;
  // Max line length for position key encoding
  int maxLineLength = 100;

  CompositionOptimizer(const Config& config,
                       double overshootPenalty = 3.0, double forwardBias = 2.0,
                       int maxLineLength = 100)
      : config(std::move(config)),
        overshootPenalty(overshootPenalty),
        forwardBias(forwardBias),
        maxLineLength(maxLineLength) {}

  // Composes edit transitions + movement. Pre-computes edit regions, then searches for optimal sequence.
  // Much slower; ~ O(n^2) + Sigma (m_i)^3, higher constant factor.
  std::vector<Result> optimize(
    const Lines& initialLines,
    const Position initialPos,
    const Lines& goalLines,
    const Position goalPos,
    const std::string& userSequence,

    const NavContext& navigationContext,
    const MotionBoundary& boundary = MotionBoundary(),
    const MotionToKeys& rawMotionToKeys = EXPLORABLE_MOTIONS,

    // Search parameters (uses struct defaults if not specified via designated initializers)
    CompositionOptimizerParams params = {}
  );
};
