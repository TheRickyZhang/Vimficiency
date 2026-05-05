#include "TransformPostExplorerEmissions.h"

#include <algorithm>

#include "Boundary/NavBoundary.h"
#include "Effort/RunningEffort.h"
#include "Keyboard/PhysicalKeys.h"
#include "Keyboard/ToKeys/MovementToKeys.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Optimizer/NavOptimizer/NavOptimizerParams.h"
#include "Types/Sequence.h"

namespace TransformPostExplorer {

std::optional<Result> tryVisualDelete(
    const Lines& effectiveLines,
    int leftColOffset,
    int rightColOffset,
    const TransformBoundary& transformBoundary,
    const TransformOptimizerParams& params,
    const Config& config) {
  const bool spansContent =
      effectiveLines.size() > 1 ||
      static_cast<int>(effectiveLines[0].size()) > leftColOffset + rightColOffset;
  if (!spansContent) return std::nullopt;

  CursorPos beginPos(0, leftColOffset);
  int lastLine = effectiveLines.lastLine();
  int lastCol = static_cast<int>(effectiveLines[lastLine].size()) - 1 - rightColOffset;
  CursorPos lastPos(lastLine, std::max(0, lastCol));

  const bool sameCell = lastPos.line == beginPos.line && lastPos.col == beginPos.col;
  if (sameCell) return std::nullopt;
  if (lastPos < beginPos) return std::nullopt;

  // Visual replay needs literal Vim endpoints. Keep line-level context for
  // absolute motions, but do not let prefix/suffix column clipping make
  // motions like `$` appear to land before the protected suffix.
  NavBoundary navBoundary(
      effectiveLines,
      CursorPos(0, 0),
      effectiveLines.endPos(),
      transformBoundary.hasLinesAbove(),
      transformBoundary.hasLinesBelow());

  // TO-REVIEW: this internal NavOptimizer call uses a default-constructed
  // NavOptimizerParams (motion-class settings, A* weights, etc.); it does
  // NOT propagate user-configured motion-class settings (fMotionThreshold,
  // useDirectionalPruning) from `params`. Same caveat as the prior inline
  // version in DeletionGoalHandler::finalize.
  NavOptimizer navOpt(config);
  auto navResult = navOpt.optimize(
      effectiveLines,
      beginPos,
      lastPos,
      NavOptimizerParams{}
          .withMinCountRepeat(params.minPrefixCount)
          .withMaxCountRepeat(params.maxPrefixCount),
      "",
      navBoundary
  );

  const auto& navResults = navResult.getResults();
  if (navResults.empty() || navResults[0].getSequence().empty()) return std::nullopt;

  Sequence visualSeq("v");
  visualSeq.append(navResults[0].getSequence().view());
  visualSeq.append("d");

  static const PhysicalKeys vKey = {Key::Key_V};
  static const PhysicalKeys dKey = {Key::Key_D};
  RunningEffort effort(vKey, config);
  effort.append(globalSequenceToKeys().tokenize(navResults[0].getSequence().view()), config);
  double totalEffort = effort.append(dKey, config);

  return Result(std::move(visualSeq), totalEffort);
}

}  // namespace TransformPostExplorer
