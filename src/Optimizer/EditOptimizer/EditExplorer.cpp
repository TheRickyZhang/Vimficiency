#include "EditExplorer.h"
#include "EditSearchContext.h"
#include "EditToSpec.h"
#include "Boundary/EditBoundary.h"
#include "Optimizer/CountPenalty.h"
#include "Optimizer/GlobalRuntimeOptions.h"
#include "VimCore/VimCore.h"
#include "VimCore/VimEditUtils.h"
#include "VimCore/VimEndpointUtils.h"
#include "VimCore/VimMotionUtils.h"
#include "VimCore/VimPortedImpl.h"

#include <algorithm>

using namespace std;

namespace {
template<CountClass C>
double resolveCountPenalty(const CountPenaltyInput& in) {
  return runtimeCountPenalty<C>(in);
}

CursorPos onePastOnSameLine(const Lines& lines, const CursorPos& inclusivePos) {
  int lineLen = static_cast<int>(lines[inclusivePos.line].size());
  if (lineLen == 0) return CursorPos(inclusivePos.line, 0);
  return CursorPos(inclusivePos.line, std::min(inclusivePos.col + 1, lineLen));
}

CursorPos normalizeForwardExclusiveStart(const CursorPos& start,
                                         const CursorPos& goal,
                                         const Lines& lines) {
  if (goal.line <= start.line) return start;
  int firstNonBlank = VimCore::firstNonBlankColInLineStr(lines[start.line]);
  if (start.col <= firstNonBlank) return CursorPos(start.line, 0);
  return start;
}

template<CountClass C>
RunningEffort buildCountedEffort(const Config& config, int count,
                                 KSId baseId, int span) {
  const KeyedSequence& baseKS = KeyedSequence::byId(baseId);
  PhysicalKeys keys;
  keys.append(CountToKeys::keysForCount(count));
  keys.append(baseKS.keys);
  RunningEffort effort(keys, config);
  CountPenaltyInput input{count, span};
  double penalty = resolveCountPenalty<C>(input);
  if (penalty > 0.0) {
    effort.addPenalty(penalty);
  }
  return effort;
}
}  // namespace

EditExplorer::EditExplorer(EditSearchContext& ctx) : ctx_(ctx) {}

bool EditExplorer::inBoundaryRegion(const CursorPos& pos, const Lines& lines) const {
  return ctx_.inBoundaryRegion(pos, lines);
}

pair<int, int> EditExplorer::computeEditBounds(const Lines& lines, const CursorPos& cursor) const {
  return ctx_.computeEditBounds(lines, cursor);
}

// =============================================================================
// Exploration Helper Methods
// =============================================================================

void EditExplorer::exploreTextObjectEdits(
    const vector<Edit::TextObjectEditSpec>& specs,
    const CursorPos& cursor, const Lines& lines, DeletionCallback onDeletion) {
  for (const auto& spec : specs) {
    Range range = VimCore::textObjectRange(
        cursor, lines, spec.isInner, spec.isBig,
        ctx_.leftColOffset, ctx_.rightColOffset,
        ctx_.editBoundary.hasLinesAbove(), ctx_.editBoundary.hasLinesBelow());

    if (range.begin == POSITION_OUTSIDE_BOUNDARY || range.end == POSITION_OUTSIDE_BOUNDARY)
      continue;

    onDeletion(range, SequenceBinding(KeyedSequence::byId(spec.ksId), ctx_.effortFor(spec.ksId)));
  }
}

void EditExplorer::exploreHalfLineEdits(
    const vector<Edit::LineEditSpec>& specs,
    const CursorPos& cursor, const Lines& lines, int contentStart, int contentEnd,
    DeletionCallback onDeletion) {
  int lastEditLine = lines.lastLine();

  for (const Edit::LineEditSpec& spec : specs) {
    if (spec.ksId == KSId::D) {
      if (cursor.line == lastEditLine && ctx_.editBoundary.hasSuffix()) continue;

      int lineLen = static_cast<int>(lines[cursor.line].size());
      int lineContentEnd = lineLen;
      if (cursor.line == lastEditLine && ctx_.rightColOffset > 0) {
        lineContentEnd -= ctx_.rightColOffset;
      }
      if (lineContentEnd <= 0) continue;

      int endCol = lineContentEnd - 1;
      if (endCol < cursor.col) continue;
      Range range(cursor, CursorPos(cursor.line, endCol + 1));
      onDeletion(range, SequenceBinding(KeyedSequence::byId(spec.ksId), ctx_.effortFor(spec.ksId)));
    } else if (spec.ksId == KSId::d0) {
      if (cursor.line == 0 && ctx_.editBoundary.hasPrefix()) continue;

      int lineContentStart = (cursor.line == 0) ? ctx_.leftColOffset : 0;
      if (cursor.col <= lineContentStart) continue;
      Range range(CursorPos(cursor.line, lineContentStart),
                  CursorPos(cursor.line, cursor.col));
      onDeletion(range, SequenceBinding(KeyedSequence::byId(spec.ksId), ctx_.effortFor(spec.ksId)));
    } else {
      assert(false && "Unexpected half-line edit command");
    }
  }
}

void EditExplorer::exploreFullLineEdits(
    const std::vector<Edit::FullLineEditSpec>& specs,
    const CursorPos& cursor, const Lines& lines, LinewiseCallback onLinewise) {
  if ((cursor.line == 0 && ctx_.editBoundary.hasPrefix()) ||
      (cursor.line == lines.lastLine() && ctx_.editBoundary.hasSuffix())) {
    return;
  }
  for (const auto& spec : specs) {
    onLinewise(cursor.line, SequenceBinding(KeyedSequence::byId(spec.ksId), ctx_.effortFor(spec.ksId)));
  }
}

void EditExplorer::exploreCharEdits(
    const CursorPos& cursor, const Lines& lines, int contentStart, int contentEnd,
    int editContentLen, DeletionCallback onDeletion) {
  // x: delete char at cursor (must be in content region)
  if (contentStart <= cursor.col && cursor.col < contentEnd) {
    onDeletion(Range(cursor, CursorPos(cursor.line, cursor.col + 1)),
               SequenceBinding(KeyedSequence::x, ctx_.effortFor(KSId::x)));
  }

  // X: delete char before cursor
  if (cursor.col > contentStart && cursor.col <= contentEnd) {
    CursorPos before(cursor.line, cursor.col - 1);
    onDeletion(Range(before, CursorPos(before.line, before.col + 1)),
               SequenceBinding(KeyedSequence::X, ctx_.effortFor(KSId::X)));
  }
}

// Templated helper for paragraph edit exploration
template<bool Forward>
void EditExplorer::exploreParagraphEdits(
    const std::vector<Edit::ParagraphEditSpecNoDir>& specs,
    const CursorPos& cursor, const Lines& lines,
    DeletionCallback onDeletion, LinewiseCallback onLinewise) {
  int lastLine = lines.lastLine();

  bool hasLinesOutside = Forward ? ctx_.editBoundary.hasLinesBelow() : ctx_.editBoundary.hasLinesAbove();
  int endpointLine = VimCore::motionParagraphEndpoint<Forward, LineEdgeType::NextEdge>(
      cursor.line, lines, hasLinesOutside);

  if (endpointLine == VimCore::LINE_OUTSIDE_BOUNDARY) return;

  // Still need prefix/suffix check (column-level protection on edge lines)
  if constexpr (Forward) {
    if (ctx_.editBoundary.hasSuffix() && endpointLine == lastLine) return;
  } else {
    if (ctx_.editBoundary.hasPrefix() && endpointLine == 0) return;
  }

  for (const auto& spec : specs) {
    if constexpr (Forward) {
      // Mirror d} exclusive semantics from interpreter path:
      // - motion destination is paragraph-next endpoint
      // - EOF on last non-blank line is inclusive
      // - apply exclusive-linewise adjustment for col-0 endpoints
      CursorPos goalPos(endpointLine, 0);
      if (endpointLine == lastLine && !VimCore::isBlankLineStr(lines[endpointLine])) {
        goalPos.setCol(std::max(0, static_cast<int>(lines[endpointLine].size()) - 1));
      }
      CursorPos startPos = normalizeForwardExclusiveStart(cursor, goalPos, lines);

      bool atEof = (goalPos.line == lastLine) &&
                   !VimCore::isBlankLineStr(lines[goalPos.line]);
      if (!(goalPos > startPos || (atEof && goalPos >= startPos))) continue;

      Range range(startPos, goalPos);
      if (atEof) {
        int eofLineLen = static_cast<int>(lines[goalPos.line].size());
        range.end = CursorPos(goalPos.line, std::min(goalPos.col + 1, eofLineLen));
      }
      auto adj = VimCore::adjustExclusiveRange(range, lines);
      SequenceBinding cmd(KeyedSequence::byId(spec.ksId), ctx_.effortFor(spec.ksId));
      if (adj == VimCore::ExclusiveAdjust::Linewise) {
        if (onLinewise && range.end.line == range.begin.line + 1) {
          onLinewise(range.begin.line, cmd);
        } else if (range.end.line > range.begin.line + 1) {
          CursorPos charEnd(range.end.line - 1, static_cast<int>(lines[range.end.line - 1].size()));
          Range charRange(range.begin, charEnd);
          if (charRange.begin < charRange.end) onDeletion(charRange, cmd);
        }
      } else if (range.begin < range.end) {
        onDeletion(range, cmd);
      }
    } else {
      // d{: when endpointLine == cursor.line, { stays on the same line and
      // d{ is characterwise exclusive (not linewise), so skip.
      // Multi-line d{ (endpointLine < cursor.line) not yet modeled.
      if (endpointLine >= cursor.line) continue;
    }
  }
}

// Templated helper for sentence edit exploration.
// Both ) and ( are exclusive motions — ranges are adjusted via
// adjustExclusiveRange() to match Vim's exclusive-linewise rule.
// See VimEditUtils.h for the full rule description and examples.
template<bool Forward>
void EditExplorer::exploreSentenceEdits(
    const std::vector<Edit::SentenceEditSpecNoDir>& specs,
    const CursorPos& cursor, const Lines& lines,
    DeletionCallback onDeletion, LinewiseCallback onLinewise) {
  int lastLine = lines.lastLine();

  bool hasBoundaryContext = (ctx_.leftColOffset > 0 || ctx_.rightColOffset > 0 ||
                             ctx_.editBoundary.hasLinesAbove() || ctx_.editBoundary.hasLinesBelow());

  CursorPos endpoint = cursor;
  if (hasBoundaryContext) {
    int boundaryOffset = Forward ? ctx_.rightColOffset : ctx_.leftColOffset;
    bool hasLinesOutside = Forward ? ctx_.editBoundary.hasLinesBelow()
                                   : ctx_.editBoundary.hasLinesAbove();
    endpoint = VimCore::motionSentenceEndpoint<Forward, SentenceEdgeType::NextEdge>(
        cursor, lines, boundaryOffset, hasLinesOutside);
  } else {
    if constexpr (Forward) {
      CursorPos motionGoal = endpoint;
      VimCore::motionSentenceNext(motionGoal, lines);
      CursorPos endpointGoal =
          VimCore::motionSentenceEndpoint<true, SentenceEdgeType::NextEdge>(
              cursor, lines, 0, false);
      bool eofOneCharAdvance =
          (endpointGoal.line == motionGoal.line) &&
          (endpointGoal.line == lines.lastLine()) &&
          (endpointGoal.col == motionGoal.col + 1) &&
          (endpointGoal.col ==
           std::max(0, static_cast<int>(lines[endpointGoal.line].size()) - 1));
      endpoint = eofOneCharAdvance ? endpointGoal : motionGoal;
    } else {
      VimCore::motionSentencePrev(endpoint, lines);
    }
  }

  if (endpoint == POSITION_OUTSIDE_BOUNDARY) return;

  // Still need prefix/suffix check (column-level protection on edge lines)
  if constexpr (Forward) {
    if (ctx_.editBoundary.hasSuffix() && endpoint.line == lastLine &&
        endpoint.col >= static_cast<int>(lines[lastLine].size()) - ctx_.rightColOffset) return;
  } else {
    if (ctx_.editBoundary.hasPrefix() && endpoint.line == 0 &&
        endpoint.col < ctx_.leftColOffset) return;
  }

  for (const auto& spec : specs) {
    if constexpr (Forward) {
      CursorPos startPos = normalizeForwardExclusiveStart(cursor, endpoint, lines);
      // d): forward — exclusive end = endpoint (motion destination).
      if (endpoint <= startPos) continue;
      Range range(startPos, endpoint);
      bool atEof = (endpoint.line == lastLine) &&
                   !VimCore::isBlankLineStr(lines[endpoint.line]) &&
                   (endpoint.col == std::max(0, static_cast<int>(lines[endpoint.line].size()) - 1));
      if (atEof) {
        bool includeEofChar = true;
        if (endpoint.col == 0) {
          int pl = endpoint.line;
          int pc = endpoint.col;
          if (VimCore::stepBack(lines, pl, pc)) {
            unsigned char prevChar = static_cast<unsigned char>(lines[pl][pc]);
            if (VimCore::isSentenceEnd(prevChar) ||
                VimCore::isSentenceCloser(prevChar)) {
              includeEofChar = false;
            }
          }
        }
        atEof = includeEofChar;
      }
      if (atEof) {
        int lineLen = static_cast<int>(lines[endpoint.line].size());
        range.end = CursorPos(endpoint.line, std::min(endpoint.col + 1, lineLen));
      }
      auto adj = VimCore::adjustExclusiveRange(range, lines);
      SequenceBinding cmd(KeyedSequence::byId(spec.ksId), ctx_.effortFor(spec.ksId));
      if (adj == VimCore::ExclusiveAdjust::Linewise) {
        if (onLinewise && range.end.line == range.begin.line + 1) {
          onLinewise(range.begin.line, cmd);
        } else if (range.end.line > range.begin.line + 1) {
          CursorPos charEnd(range.end.line - 1, static_cast<int>(lines[range.end.line - 1].size()));
          Range charRange(range.begin, charEnd);
          if (charRange.begin < charRange.end) onDeletion(charRange, cmd);
        }
      } else if (range.begin < range.end) {
        onDeletion(range, cmd);
      }
    } else {
      // d(: backward — exclusive end = cursor (the higher end of the range).
      if (endpoint >= cursor) continue;
      Range range(endpoint, cursor);
      VimCore::adjustExclusiveRange(range, lines);
      onDeletion(range,
                 SequenceBinding(KeyedSequence::byId(spec.ksId), ctx_.effortFor(spec.ksId)));
    }
  }
}

// =============================================================================
// Templated Exploration Helpers - EdgeType known at compile time
// =============================================================================

template<EdgeType Edge>
void EditExplorer::exploreForwardWordEdits(
    const vector<Edit::ForwardWordEditSpecNoEdge>& specs,
    const CursorPos& cursor, const Lines& lines, DeletionCallback onDeletion) {
  for (const auto& spec : specs) {
    if constexpr (Edge == EdgeType::GapEdge) {
      Range range = VimCore::deleteWordForwardRangeBounded(
          cursor, lines, spec.isBig, ctx_.rightColOffset, ctx_.editBoundary.hasLinesBelow());
      if (!range.begin.isValid() || !range.end.isValid() ||
          range.begin == POSITION_OUTSIDE_BOUNDARY ||
          range.end == POSITION_OUTSIDE_BOUNDARY ||
          !(range.begin < range.end)) {
        continue;
      }
      onDeletion(range, SequenceBinding(KeyedSequence::byId(spec.ksId), ctx_.effortFor(spec.ksId)));
      continue;
    }

    CursorPos endpoint = VimCore::motionWordEndpoint<true, Edge>(
        cursor, lines, spec.isBig, spec.skipCurrent,
        ctx_.rightColOffset, ctx_.editBoundary.hasLinesBelow(), /*lineBounded=*/false);

    if (endpoint == POSITION_OUTSIDE_BOUNDARY || endpoint == cursor)
      continue;

    onDeletion(Range(cursor, onePastOnSameLine(lines, endpoint)),
               SequenceBinding(KeyedSequence::byId(spec.ksId), ctx_.effortFor(spec.ksId)));
  }
}

template<EdgeType Edge>
void EditExplorer::exploreBackwardWordEdits(
    const vector<Edit::BackwardWordEditSpecNoEdge>& specs,
    const CursorPos& cursor, const Lines& lines, DeletionCallback onDeletion) {
  for (const auto& spec : specs) {
    if (spec.isExclusiveAtCursor) {
      Range range = VimCore::deleteWordBackwardRangeBounded(
          cursor, lines, spec.isBig, ctx_.leftColOffset, ctx_.editBoundary.hasLinesAbove());
      if (!range.begin.isValid() || !range.end.isValid() || !(range.begin < range.end)) {
        continue;
      }
      onDeletion(range, SequenceBinding(KeyedSequence::byId(spec.ksId), ctx_.effortFor(spec.ksId)));
      continue;
    }

    // For inclusive backward deletes (dge/dgE), check cursor first
    if (inBoundaryRegion(cursor, lines))
      continue;

    CursorPos endpoint = VimCore::motionWordEndpoint<false, Edge>(
        cursor, lines, spec.isBig, spec.skipCurrent,
        ctx_.leftColOffset, ctx_.editBoundary.hasLinesAbove(), /*lineBounded=*/false);

    if (endpoint == POSITION_OUTSIDE_BOUNDARY || endpoint == cursor)
      continue;

    Range range(endpoint, onePastOnSameLine(lines, cursor));

    onDeletion(range, SequenceBinding(KeyedSequence::byId(spec.ksId), ctx_.effortFor(spec.ksId)));
  }
}

// Explicit template instantiations
template void EditExplorer::exploreForwardWordEdits<EdgeType::WordEdge>(
    const vector<Edit::ForwardWordEditSpecNoEdge>&, const CursorPos&, const Lines&, DeletionCallback);
template void EditExplorer::exploreForwardWordEdits<EdgeType::GapEdge>(
    const vector<Edit::ForwardWordEditSpecNoEdge>&, const CursorPos&, const Lines&, DeletionCallback);
template void EditExplorer::exploreBackwardWordEdits<EdgeType::WordEdge>(
    const vector<Edit::BackwardWordEditSpecNoEdge>&, const CursorPos&, const Lines&, DeletionCallback);
template void EditExplorer::exploreBackwardWordEdits<EdgeType::NextEdge>(
    const vector<Edit::BackwardWordEditSpecNoEdge>&, const CursorPos&, const Lines&, DeletionCallback);
template void EditExplorer::exploreParagraphEdits<true>(
    const vector<Edit::ParagraphEditSpecNoDir>&, const CursorPos&, const Lines&, DeletionCallback, LinewiseCallback);
template void EditExplorer::exploreParagraphEdits<false>(
    const vector<Edit::ParagraphEditSpecNoDir>&, const CursorPos&, const Lines&, DeletionCallback, LinewiseCallback);
template void EditExplorer::exploreSentenceEdits<true>(
    const vector<Edit::SentenceEditSpecNoDir>&, const CursorPos&, const Lines&, DeletionCallback, LinewiseCallback);
template void EditExplorer::exploreSentenceEdits<false>(
    const vector<Edit::SentenceEditSpecNoDir>&, const CursorPos&, const Lines&, DeletionCallback, LinewiseCallback);

// =============================================================================
// Counted Exploration Methods
// =============================================================================

void EditExplorer::exploreCountedLineEdits(
    const CursorPos& cursor, const Lines& lines,
    int minCountRepeat,
    CountedLinewiseCallback onCountedLinewise) {
  if (!onCountedLinewise) return;
  const int maxCountRepeat = ctx_.params.maxPrefixCount;

  int lastLine = lines.lastLine();
  bool hasPrefix = ctx_.editBoundary.hasPrefix();
  bool hasSuffix = ctx_.editBoundary.hasSuffix();

  // dj: 2-line delete downward (always explored, it's 2 keystrokes not a counted command)
  if (cursor.line + 1 <= lastLine) {
    bool skipDj = (cursor.line == 0 && hasPrefix) ||
                  (cursor.line + 1 == lastLine && hasSuffix);
    if (!skipDj) {
      onCountedLinewise(LineRange(cursor.line, cursor.line + 2),
                        SequenceBinding(KeyedSequence::dj, ctx_.effortFor(KSId::dj)));
    }
  }

  // dk: 2-line delete upward (always explored, it's 2 keystrokes not a counted command)
  if (cursor.line - 1 >= 0) {
    bool skipDk = (cursor.line - 1 == 0 && hasPrefix) ||
                  (cursor.line == lastLine && hasSuffix);
    if (!skipDk) {
      onCountedLinewise(LineRange(cursor.line - 1, cursor.line + 1),
                        SequenceBinding(KeyedSequence::dk, ctx_.effortFor(KSId::dk)));
    }
  }

  // {n}dd: counted line delete (n >= minCountRepeat)
  if (cursor.line == 0 && hasPrefix) return;

  // Find last deletable line (respecting suffix boundary)
  int lastDeletable = lastLine;
  if (hasSuffix) lastDeletable = lastLine - 1;
  if (lastDeletable < cursor.line) return;

  int maxCount = lastDeletable - cursor.line + 1;
  maxCount = min(maxCount, maxCountRepeat);
  for (int n = max(2, minCountRepeat); n <= maxCount; n++) {
    RunningEffort countedEffort =
        buildCountedEffort<CountClass::EditLine>(ctx_.config, n, KSId::dd, n);
    onCountedLinewise(LineRange(cursor.line, cursor.line + n),
                      SequenceBinding(KeyedSequence::dd, countedEffort, n));
  }
}

void EditExplorer::exploreCountedJoinCommands(
    const CursorPos& cursor, const Lines& lines,
    int minCountRepeat,
    CountedJoinCallback onCountedJoin) {
  if (!onCountedJoin) return;
  const int maxCountRepeat = ctx_.params.maxPrefixCount;

  int lastLine = lines.lastLine();
  if (cursor.line >= lastLine) return;

  // Find max count: need count-1 lines below cursor
  int maxLinesBelow = lastLine - cursor.line;

  // Don't join into suffix boundary
  if (ctx_.editBoundary.hasLinesBelow() && cursor.line + maxLinesBelow == lastLine) {
    maxLinesBelow--;
  }
  // Also check suffix on the last line in the range
  if (ctx_.editBoundary.hasSuffix()) {
    // Can't join the last line if it has suffix
    if (cursor.line + maxLinesBelow == lastLine) {
      maxLinesBelow--;
    }
  }

  if (maxLinesBelow < 1) return;

  // count means "join count lines" (current + count-1 below)
  // so count=2 joins current + 1 below, count=3 joins current + 2 below, etc.
  int maxCount = min(maxLinesBelow + 1, maxCountRepeat);
  for (int count = max(2, minCountRepeat); count <= maxCount; count++) {
    RunningEffort jEffort =
        buildCountedEffort<CountClass::Join>(ctx_.config, count, KSId::J, count);
    onCountedJoin(true, SequenceBinding(KeyedSequence::J, jEffort, count));

    RunningEffort gjEffort =
        buildCountedEffort<CountClass::Join>(ctx_.config, count, KSId::gJ, count);
    onCountedJoin(false, SequenceBinding(KeyedSequence::gJ, gjEffort, count));
  }
}

void EditExplorer::exploreCountedWordEdits(
    const CursorPos& cursor, const Lines& lines,
    int minCountRepeat,
    DeletionCallback onDeletion) {
  if (!onDeletion) return;
  const int maxCountRepeat = ctx_.params.maxPrefixCount;
  if (minCountRepeat < 2 || maxCountRepeat < 2) return;

  static constexpr int MAX_COUNT_ITERATIONS = 9;
  const int maxIterations = min(MAX_COUNT_ITERATIONS, maxCountRepeat);

  // Use raw motion functions (motionE, motionW, motionB, motionGe) to match
  // Edit::applyEdit exactly. The boundary-aware motionWordEndpoint can diverge
  // from applyEdit after A* deletions change the buffer structure.
  //
  // Only emit the max valid count per motion type (the furthest reachable
  // endpoint that stays on the same line). Intermediate counts add branching
  // with little benefit — they're reachable via uncounted + dot repeat.

  // Forward word-end edits: {n}de, {n}dE
  for (const auto& spec : Edit::FORWARD_WORDEDGE_EDITS) {
    CursorPos prev = cursor;
    CursorPos lastEndpoint;
    int lastCount = 0;
    for (int count = 1; count <= maxIterations; count++) {
      CursorPos endpoint = prev;
      VimCore::motionE(endpoint, lines, spec.isBig);

      if (endpoint == prev) break;
      if (endpoint.line != cursor.line) break;

      // Handle past-end position (clamp to last valid char)
      int lineLen = static_cast<int>(lines[endpoint.line].size());
      if (endpoint.col >= lineLen) endpoint.setCol(lineLen - 1);

      if (inBoundaryRegion(endpoint, lines)) break;

      lastEndpoint = endpoint;
      lastCount = count;
      prev = endpoint;
    }
    if (lastCount >= minCountRepeat) {
      RunningEffort countedEffort = spec.isBig
          ? buildCountedEffort<CountClass::EditWORD>(ctx_.config, lastCount, spec.ksId, lastCount)
          : buildCountedEffort<CountClass::EditWord>(ctx_.config, lastCount, spec.ksId, lastCount);
      onDeletion(Range(cursor, onePastOnSameLine(lines, lastEndpoint)),
                 SequenceBinding(KeyedSequence::byId(spec.ksId), countedEffort, lastCount));
    }
  }

  // Forward word-gap edits: {n}dw, {n}dW
  for (const auto& spec : Edit::FORWARD_GAPEDGE_EDITS) {
    CursorPos prev = cursor;
    CursorPos lastEnd;
    int lastCount = 0;
    for (int count = 1; count <= maxIterations; count++) {
      CursorPos endpoint = prev;
      VimCore::motionW(endpoint, lines, spec.isBig);

      int lineLen = static_cast<int>(lines[endpoint.line].size());
      bool stalled = (endpoint == prev);
      bool stalledAtEof = stalled &&
                          (endpoint.line == lines.lastLine()) &&
                          (lineLen > 0) &&
                          (endpoint.col == lineLen - 1);
      if (stalled && !stalledAtEof) break;
      if (!stalled && endpoint.line != cursor.line) break;

      CursorPos end;
      if (stalledAtEof) {
        // Extra count exhausted at EOF: include the last character.
        end = CursorPos(endpoint.line, lineLen);
      } else {
        end = endpoint;
      }

      CursorPos boundaryCheckPos(
          endpoint.line, lineLen == 0 ? 0 : std::max(0, end.col - 1));
      if (inBoundaryRegion(boundaryCheckPos, lines)) break;

      lastEnd = end;
      lastCount = count;
      if (stalledAtEof) break;
      prev = endpoint;  // Next iteration starts from motionW's destination
    }
    if (lastCount >= minCountRepeat) {
      RunningEffort countedEffort = spec.isBig
          ? buildCountedEffort<CountClass::EditWORD>(ctx_.config, lastCount, spec.ksId, lastCount)
          : buildCountedEffort<CountClass::EditWord>(ctx_.config, lastCount, spec.ksId, lastCount);
      onDeletion(Range(cursor, lastEnd),
                 SequenceBinding(KeyedSequence::byId(spec.ksId), countedEffort, lastCount));
    }
  }

  // Backward word edits: {n}db, {n}dB
  for (const auto& spec : Edit::BACKWARD_WORDEDGE_EDITS) {
    CursorPos prev = cursor;
    CursorPos lastEndpoint;
    int lastCount = 0;
    for (int count = 1; count <= maxIterations; count++) {
      CursorPos endpoint = prev;
      VimCore::motionB(endpoint, lines, spec.isBig);

      if (endpoint == prev) break;
      if (endpoint.line != cursor.line) break;

      if (inBoundaryRegion(endpoint, lines)) break;

      lastEndpoint = endpoint;
      lastCount = count;
      prev = endpoint;
    }
    if (lastCount >= minCountRepeat) {
      // db/dB are exclusive at cursor: range is [endpoint, cursor-1]
      if (cursor.col > (cursor.line == 0 ? ctx_.leftColOffset : 0)) {
        Range range(lastEndpoint, CursorPos(cursor.line, cursor.col));
        RunningEffort countedEffort = spec.isBig
            ? buildCountedEffort<CountClass::EditWORD>(ctx_.config, lastCount, spec.ksId, lastCount)
            : buildCountedEffort<CountClass::EditWord>(ctx_.config, lastCount, spec.ksId, lastCount);
        onDeletion(range, SequenceBinding(KeyedSequence::byId(spec.ksId), countedEffort, lastCount));
      }
    }
  }

  // Backward word-end edits: {n}dge, {n}dgE
  for (const auto& spec : Edit::BACKWARD_NEXTEDGE_EDITS) {
    if (!spec.isExclusiveAtCursor && inBoundaryRegion(cursor, lines))
      continue;

    CursorPos prev = cursor;
    CursorPos lastEndpoint;
    int lastCount = 0;
    for (int count = 1; count <= maxIterations; count++) {
      CursorPos endpoint = prev;
      VimCore::motionGe(endpoint, lines, spec.isBig);

      if (endpoint == prev) break;
      if (endpoint.line != cursor.line) break;

      if (inBoundaryRegion(endpoint, lines)) break;

      lastEndpoint = endpoint;
      lastCount = count;
      prev = endpoint;
    }
    if (lastCount >= minCountRepeat) {
      RunningEffort countedEffort = spec.isBig
          ? buildCountedEffort<CountClass::EditWORD>(ctx_.config, lastCount, spec.ksId, lastCount)
          : buildCountedEffort<CountClass::EditWord>(ctx_.config, lastCount, spec.ksId, lastCount);
      onDeletion(Range(lastEndpoint, onePastOnSameLine(lines, cursor)),
                 SequenceBinding(KeyedSequence::byId(spec.ksId), countedEffort, lastCount));
    }
  }
}

void EditExplorer::exploreCountedCharEdits(
    const CursorPos& cursor, const Lines& lines,
    int contentStart, int contentEnd,
    int minCountRepeat,
    DeletionCallback onDeletion) {
  if (!onDeletion) return;
  const int maxCountRepeat = ctx_.params.maxPrefixCount;
  if (minCountRepeat < 2 || maxCountRepeat < 2) return;

  // Only x (forward delete at cursor)
  if (contentStart > cursor.col || cursor.col >= contentEnd) return;

  // Only emit the max count (delete all remaining content chars from cursor).
  // Intermediate counts are reachable via uncounted x + dot repeat.
  static constexpr int MAX_COUNT_DIGIT = 9;
  int count = min(contentEnd - cursor.col, min(MAX_COUNT_DIGIT, maxCountRepeat));
  if (count < minCountRepeat) return;

  Range range(cursor, CursorPos(cursor.line, cursor.col + count));
  RunningEffort effort =
      buildCountedEffort<CountClass::EditChar>(ctx_.config, count, KSId::x, count);
  onDeletion(range, SequenceBinding(KeyedSequence::x, effort, count));
}

// =============================================================================
// Main Exploration Entry Point
// =============================================================================

void EditExplorer::exploreJoinCommands(
    const CursorPos& cursor, const Lines& lines, JoinCallback onJoin) {
  if (!onJoin) return;

  // J/gJ require a next line to join with
  if (cursor.line >= lines.lastLine()) return;

  int nextLine = cursor.line + 1;

  // Don't allow J if there are lines below the edit region
  if (nextLine == lines.lastLine() && ctx_.editBoundary.hasLinesBelow()) return;

  // Explore both J (with space) and gJ (without space)
  onJoin(true, SequenceBinding(KeyedSequence::J, ctx_.effortFor(KSId::J)));
  onJoin(false, SequenceBinding(KeyedSequence::gJ, ctx_.effortFor(KSId::gJ)));
}

void EditExplorer::exploreAllDeletions(const EditState& state,
                                       DeletionCallback onDeletion,
                                       LinewiseCallback onLinewise,
                                       JoinCallback onJoin) {
  const Lines& lines = state.getLines();
  CursorPos cursor = state.getPos();

  auto [contentBegin, contentEnd] = computeEditBounds(lines, cursor);
  int editContentLen = contentEnd - contentBegin;

  // Empty editable content on a truly empty line. Explore limited set.
  if (editContentLen <= 0) {
    assert(lines[cursor.line].empty());

    exploreFullLineEdits(Edit::EMPTYLINE_FULL_LINE_EDITS, cursor, lines, onLinewise);
    exploreForwardWordEdits<EdgeType::WordEdge>(Edit::FORWARD_WORDEDGE_EDITS, cursor, lines, onDeletion);
    exploreBackwardWordEdits<EdgeType::WordEdge>(Edit::BACKWARD_WORDEDGE_EDITS, cursor, lines, onDeletion);
    exploreBackwardWordEdits<EdgeType::NextEdge>(Edit::BACKWARD_NEXTEDGE_EDITS, cursor, lines, onDeletion);
    exploreJoinCommands(cursor, lines, onJoin);
    return;
  }

  // Full exploration
  exploreForwardWordEdits<EdgeType::WordEdge>(Edit::FORWARD_WORDEDGE_EDITS, cursor, lines, onDeletion);
  exploreForwardWordEdits<EdgeType::GapEdge>(Edit::FORWARD_GAPEDGE_EDITS, cursor, lines, onDeletion);
  exploreBackwardWordEdits<EdgeType::WordEdge>(Edit::BACKWARD_WORDEDGE_EDITS, cursor, lines, onDeletion);
  exploreBackwardWordEdits<EdgeType::NextEdge>(Edit::BACKWARD_NEXTEDGE_EDITS, cursor, lines, onDeletion);
  exploreTextObjectEdits(Edit::TEXT_OBJECT_EDITS, cursor, lines, onDeletion);
  exploreHalfLineEdits(Edit::HALF_LINE_EDITS, cursor, lines, contentBegin, contentEnd, onDeletion);
  exploreFullLineEdits(Edit::FULL_LINE_EDITS, cursor, lines, onLinewise);
  exploreCharEdits(cursor, lines, contentBegin, contentEnd, editContentLen, onDeletion);
  exploreParagraphEdits<true>(Edit::FORWARD_PARAGRAPH_EDITS, cursor, lines, onDeletion, onLinewise);
  exploreParagraphEdits<false>(Edit::BACKWARD_PARAGRAPH_EDITS, cursor, lines, onDeletion, onLinewise);
  exploreSentenceEdits<true>(Edit::FORWARD_SENTENCE_EDITS, cursor, lines, onDeletion, onLinewise);
  exploreSentenceEdits<false>(Edit::BACKWARD_SENTENCE_EDITS, cursor, lines, onDeletion, onLinewise);
  exploreJoinCommands(cursor, lines, onJoin);
}
