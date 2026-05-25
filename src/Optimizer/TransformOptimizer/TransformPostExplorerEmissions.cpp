#include "TransformPostExplorerEmissions.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <utility>
#include <vector>

#include "Effort/RunningEffort.h"
#include "Keyboard/KeyedSequence.h"
#include "Keyboard/PhysicalKeys.h"
#include "Types/CharRange.h"
#include "Types/Sequence.h"
#include "VimCore/VimEditUtils.h"
#include "VimCore/VimMotionUtils.h"

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

std::optional<VisualDeleteResult> tryVisualDelete(
    const Lines& effectiveLines,
    int leftColOffset,
    int rightColOffset,
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

  Sequence visualSeq("v");
  KeyedSequence motion;

  // Visual mode changes endpoints for some motions (`)`/`}`/`$` cases), so
  // this shortcut uses only primitive cursor steps whose visual replay matches.
  auto appendMotion = [&](const KeyedSequence& ks, int count) {
    if (count <= 0) return;
    if (params.countPrefixesEnabled() &&
        count >= params.minPrefixCount && count <= params.maxPrefixCount) {
      motion.appendCounted(count, ks);
    } else {
      motion.append(ks, count);
    }
  };

  int lineDelta = lastPos.line - beginPos.line;
  appendMotion(lineDelta >= 0 ? KeyedSequence::j : KeyedSequence::k,
               std::abs(lineDelta));

  int cursorCol = beginPos.col;
  if (lineDelta != 0) {
    cursorCol = VimCore::clampCol(effectiveLines, beginPos.col, lastPos.line);
  }
  int colDelta = lastPos.col - cursorCol;
  appendMotion(colDelta >= 0 ? KeyedSequence::l : KeyedSequence::h,
               std::abs(colDelta));

  if (motion.seq.empty()) return std::nullopt;

  visualSeq.append(motion.seq.view());
  visualSeq.append("d");

  static const PhysicalKeys vKey = {Key::Key_V};
  static const PhysicalKeys dKey = {Key::Key_D};
  RunningEffort effort(vKey, config);
  effort.append(motion.keys, config);
  double totalEffort = effort.append(dKey, config);

  Lines replayLines = effectiveLines;
  CursorPos replayPos = beginPos;
  const int endLineSize = static_cast<int>(effectiveLines[lastPos.line].size());
  CharRange range(beginPos, CursorPos(lastPos.line, std::min(lastPos.col + 1, endLineSize)));
  VimCore::deleteRangeAndUpdatePos(replayLines, range, replayPos);

  return VisualDeleteResult{
      .result = Result(std::move(visualSeq), totalEffort),
      .goalPos = replayPos,
  };
}

}  // namespace TransformPostExplorer
