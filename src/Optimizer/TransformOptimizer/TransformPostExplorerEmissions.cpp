#include "TransformPostExplorerEmissions.h"

#include <algorithm>
#include <cassert>
#include <utility>
#include <vector>

#include "Boundary/NavBoundary.h"
#include "Effort/RunningEffort.h"
#include "Keyboard/KeyedSequence.h"
#include "Keyboard/PhysicalKeys.h"
#include "Keyboard/ToKeys/MovementToKeys.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Optimizer/NavOptimizer/NavOptimizerParams.h"
#include "Types/Sequence.h"

namespace TransformPostExplorer {

std::optional<Result> tryReplacement(
    std::string_view deleted,
    std::string_view inserted,
    const Config& config,
    double maxEffort) {
  if (deleted.size() != inserted.size() || deleted == inserted) return std::nullopt;
  assert(deleted.size() == inserted.size());
  assert(deleted != inserted);

  std::vector<int> diff;
  for (size_t i = 0; i < deleted.size(); i++) {
    if (deleted[i] != inserted[i]) diff.push_back(static_cast<int>(i));
  }

  KeyedSequence ks;

  auto appendNav = [&](int dist) {
    if (dist <= 2) ks.append(KeyedSequence::l, dist);
    else ks.appendCounted(dist, KeyedSequence::l);
  };

  if (diff[0] > 0) appendNav(diff[0]);

  size_t i = 0;
  while (i < diff.size()) {
    size_t j = i;
    while (j + 1 < diff.size() && diff[j + 1] == diff[j] + 1 &&
           inserted[diff[j + 1]] == inserted[diff[j]]) {
      j++;
    }

    int runLength = static_cast<int>(j - i + 1);
    if (runLength > 1) ks.appendCounted(runLength, KeyedSequence::r);
    else ks += KeyedSequence::r;
    ks.append(inserted[diff[i]]);

    i = j + 1;
    if (i < diff.size()) {
      int dist = diff[i] - diff[j];
      if (dist <= 2) {
        ks.append(KeyedSequence::l, dist);
      } else {
        char findChar = deleted[diff[i]];
        int occurrences = std::count(deleted.begin() + diff[j] + 1,
                                     deleted.begin() + diff[i], findChar);
        if (occurrences == 0) {
          ks += KeyedSequence::f;
          ks.append(findChar);
        } else {
          ks.appendCounted(dist, KeyedSequence::l);
        }
      }
    }
  }

  int lastDiff = diff.back();
  int endPos = static_cast<int>(inserted.size()) - 1;
  if (lastDiff < endPos) appendNav(endPos - lastDiff);

  RunningEffort effort(ks.keys, config);
  double totalEffort = effort.getEffort(config);
  if (totalEffort > maxEffort) return std::nullopt;

  return Result(std::move(ks.seq), totalEffort);
}

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

  // Visual-delete is a narrow post-explorer emission, not a general nav
  // surface. It inherits transform count-prefix limits only; nav-only
  // motion-class controls are intentionally not configurable here.
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
