#pragma once

#include <algorithm>

#include "Boundary/TransformBoundary.h"
#include "Effort/EffortBank.h"
#include "TransformOptimizerParams.h"
#include "EditToSpec.h"
#include "Keyboard/Config.h"
#include "Keyboard/KeyedSequence.h"
#include "Optimizer/CountPenalty.h"
#include "Optimizer/TransformOptimizer/TransformState.h"
#include "Optimizer/GlobalRuntimeOptions.h"
#include "Optimizer/SequenceBinding.h"
#include "Types/CharLineRange.h"
#include "Types/CharRange.h"
#include "Types/CursorPos.h"
#include "Types/LineCharRange.h"
#include "Types/LineRange.h"
#include "Types/Lines.h"
#include "VimCore/VimCore.h"
#include "VimCore/VimEditUtils.h"
#include "VimCore/VimEndpointUtils.h"
#include "VimCore/VimMotionUtils.h"

// TODO: enumerate insert-mode-with-correction replacement strategies.
//   For a 1-char replacement diff like `n`→`m`, today we emit `rm`,
//   `sm<Esc>`, `cl m<Esc>`, `Cm<Esc>`, etc. — all commands that enter
//   insert mode (or replace mode) at the target and produce the new
//   content directly. We DON'T enumerate strategies that enter insert
//   mode adjacent to the target and use `<BS>`/`<Del>` to reshape
//   existing content:
//     - `a<BS>m<Esc>`   (append, backspace over `n`, type `m`)
//     - `i<Del>m<Esc>`  (insert, delete `n`, type `m`)
//   These are valid Vim idioms the explore UI should teach. Adding them
//   requires a new spec category in EditToSpec + corresponding emission
//   hooks in TransformExplorer; effort is bounded to the 1-to-N-char
//   replacement family. Scoped separately from the dedup-by-sequence fix.
class TransformExplorer {
public:
  TransformExplorer(const TransformBoundary& boundary,
               const TransformOptimizerParams& params,
               const Config& config,
               const EffortBank& bank,
               int leftColOffset,
               int rightColOffset)
      : boundary_(boundary),
        params_(params),
        config_(config),
        bank_(bank),
        leftColOffset_(leftColOffset),
        rightColOffset_(rightColOffset) {}

  template<class State, class OnAnyDeletion, class OnLinewise, class OnJoin>
  void exploreAllDeletions(const State& state,
                           OnAnyDeletion&& onAnyDeletion,
                           OnLinewise&& onLinewise,
                           OnJoin&& onJoin);

  template<class OnJoin>
  void exploreJoinCommands(const CursorPos& cursor, const Lines& lines, OnJoin&& onJoin);

  template<class OnCountedLinewise>
  void exploreCountedLineEdits(const CursorPos& cursor, const Lines& lines,
                               int minCountRepeat,
                               OnCountedLinewise&& onCountedLinewise);

  template<class OnCountedJoin>
  void exploreCountedJoinCommands(const CursorPos& cursor, const Lines& lines,
                                  int minCountRepeat,
                                  OnCountedJoin&& onCountedJoin);

  template<class OnDeletion>
  void exploreCountedWordEdits(const CursorPos& cursor, const Lines& lines,
                               int minCountRepeat,
                               OnDeletion&& onDeletion);

  template<class OnDeletion>
  void exploreCountedCharEdits(const CursorPos& cursor, const Lines& lines,
                               int contentStart, int contentEnd,
                               int minCountRepeat,
                               OnDeletion&& onDeletion);

  template<class OnAnyDeletion, class OnLinewise>
  void exploreWordEdits(
      const std::vector<Edit::WordEditSpec>& specs,
      const CursorPos& cursor, const Lines& lines,
      OnAnyDeletion&& onAnyDeletion, OnLinewise&& onLinewise);

  template<bool Forward, class OnAnyDeletion, class OnLinewise>
  void exploreParagraphEdits(
      const std::vector<Edit::ParagraphEditSpecNoDir>& specs,
      const CursorPos& cursor, const Lines& lines,
      OnAnyDeletion&& onAnyDeletion,
      OnLinewise&& onLinewise);

  template<bool Forward, class OnAnyDeletion, class OnLinewise>
  void exploreSentenceEdits(
      const std::vector<Edit::SentenceEditSpecNoDir>& specs,
      const CursorPos& cursor, const Lines& lines,
      OnAnyDeletion&& onAnyDeletion,
      OnLinewise&& onLinewise);

  template<class OnDeletion>
  void exploreTextObjectEdits(
      const std::vector<Edit::TextObjectEditSpec>& specs,
      const CursorPos& cursor, const Lines& lines, OnDeletion&& onDeletion);

  template<class OnDeletion>
  void exploreHalfLineEdits(
      const std::vector<Edit::LineEditSpec>& specs,
      const CursorPos& cursor, const Lines& lines,
      int contentStart, int contentEnd, OnDeletion&& onDeletion);

  template<class OnLinewise>
  void exploreFullLineEdits(
      const std::vector<Edit::FullLineEditSpec>& specs,
      const CursorPos& cursor, const Lines& lines, OnLinewise&& onLinewise);

  template<class OnDeletion>
  void exploreCharEdits(
      const CursorPos& cursor, const Lines& lines,
      int contentStart, int contentEnd, int editContentLen,
      OnDeletion&& onDeletion);

private:
  const RunningEffort& effortFor(KSId id) const { return bank_[id]; }
  bool inBoundaryRegion(const CursorPos& pos, const Lines& lines) const;
  VimCore::WordBoundaryContext wordBoundaryContext() const {
    VimCore::WordBoundaryContext boundary;
    boundary.leftColOffset = leftColOffset_;
    boundary.rightColOffset = rightColOffset_;
    boundary.hasLinesAbove = boundary_.hasLinesAbove();
    boundary.hasLinesBelow = boundary_.hasLinesBelow();
    boundary.clampOutside = false;
    return boundary;
  }
  std::pair<int, int> computeTransformBounds(const Lines& lines, const CursorPos& cursor) const;

  const TransformBoundary& boundary_;
  const TransformOptimizerParams& params_;
  const Config& config_;
  const EffortBank& bank_;
  int leftColOffset_ = 0;
  int rightColOffset_ = 0;
};

namespace TransformExplorerDetail {

template<CountClass C>
double resolveCountPenalty(const CountPenaltyInput& in) {
  return runtimeCountPenalty<C>(in);
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

template<class OnAnyDeletion, class OnLinewise>
void emitResolvedDeletion(const VimCore::ResolvedDeleteRange& resolved,
                          const SequenceBinding& cmd,
                          OnAnyDeletion&& onAnyDeletion,
                          OnLinewise&& onLinewise) {
  switch (resolved.kind) {
    case VimCore::ResolvedDeleteRangeKind::Characterwise:
      onAnyDeletion(resolved.charRange, cmd);
      return;
    case VimCore::ResolvedDeleteRangeKind::CharLine:
      onAnyDeletion(resolved.charLineRange, cmd);
      return;
    case VimCore::ResolvedDeleteRangeKind::LineChar:
      onAnyDeletion(resolved.lineCharRange, cmd);
      return;
    case VimCore::ResolvedDeleteRangeKind::Linewise:
      onLinewise(resolved.lineRange, cmd);
      return;
  }
}

}  // namespace TransformExplorerDetail

template<class OnDeletion>
void TransformExplorer::exploreTextObjectEdits(
    const std::vector<Edit::TextObjectEditSpec>& specs,
    const CursorPos& cursor, const Lines& lines, OnDeletion&& onDeletion) {
  for (const auto& spec : specs) {
    CharRange range = VimCore::wordTextObjectRange(
        cursor, lines,
        spec.isInner ? VimCore::WordTextObjectKind::Inner
                     : VimCore::WordTextObjectKind::Around,
        spec.isBig, wordBoundaryContext());

    if (range.begin == POSITION_OUTSIDE_BOUNDARY || range.end == POSITION_OUTSIDE_BOUNDARY) {
      continue;
    }

    onDeletion(range, SequenceBinding(KeyedSequence::byId(spec.ksId), effortFor(spec.ksId)));
  }
}

template<class OnDeletion>
void TransformExplorer::exploreHalfLineEdits(
    const std::vector<Edit::LineEditSpec>& specs,
    const CursorPos& cursor, const Lines& lines, int, int,
    OnDeletion&& onDeletion) {
  int lastEditLine = lines.lastLine();

  for (const Edit::LineEditSpec& spec : specs) {
    if (spec.ksId == KSId::D) {
      if (cursor.line == lastEditLine && boundary_.hasSuffix()) continue;

      int lineLen = static_cast<int>(lines[cursor.line].size());
      int lineContentEnd = lineLen;
      if (cursor.line == lastEditLine && rightColOffset_ > 0) {
        lineContentEnd -= rightColOffset_;
      }
      if (lineContentEnd <= 0) continue;

      int endCol = lineContentEnd - 1;
      if (endCol < cursor.col) continue;
      CharRange range(cursor, CursorPos(cursor.line, endCol + 1));
      onDeletion(range, SequenceBinding(KeyedSequence::byId(spec.ksId), effortFor(spec.ksId)));
    } else if (spec.ksId == KSId::d0) {
      if (cursor.line == 0 && boundary_.hasPrefix()) continue;

      int lineContentStart = (cursor.line == 0) ? leftColOffset_ : 0;
      if (cursor.col <= lineContentStart) continue;
      CharRange range(CursorPos(cursor.line, lineContentStart),
                      CursorPos(cursor.line, cursor.col));
      onDeletion(range, SequenceBinding(KeyedSequence::byId(spec.ksId), effortFor(spec.ksId)));
    } else {
      assert(false && "Unexpected half-line edit command");
    }
  }
}

template<class OnLinewise>
void TransformExplorer::exploreFullLineEdits(
    const std::vector<Edit::FullLineEditSpec>& specs,
    const CursorPos& cursor, const Lines& lines, OnLinewise&& onLinewise) {
  if ((cursor.line == 0 && boundary_.hasPrefix()) ||
      (cursor.line == lines.lastLine() && boundary_.hasSuffix())) {
    return;
  }
  for (const auto& spec : specs) {
    onLinewise(LineRange(cursor.line, cursor.line + 1),
               SequenceBinding(KeyedSequence::byId(spec.ksId), effortFor(spec.ksId)));
  }
}

template<class OnDeletion>
void TransformExplorer::exploreCharEdits(
    const CursorPos& cursor, const Lines& lines, int contentStart, int contentEnd,
    int, OnDeletion&& onDeletion) {
  if (contentStart <= cursor.col && cursor.col < contentEnd) {
    onDeletion(CharRange(cursor, CursorPos(cursor.line, cursor.col + 1)),
               SequenceBinding(KeyedSequence::x, effortFor(KSId::x)));
  }

  if (cursor.col > contentStart && cursor.col <= contentEnd) {
    CursorPos before(cursor.line, cursor.col - 1);
    onDeletion(CharRange(before, CursorPos(before.line, before.col + 1)),
               SequenceBinding(KeyedSequence::X, effortFor(KSId::X)));
  }
}

template<bool Forward, class OnAnyDeletion, class OnLinewise>
void TransformExplorer::exploreParagraphEdits(
    const std::vector<Edit::ParagraphEditSpecNoDir>& specs,
    const CursorPos& cursor, const Lines& lines,
    OnAnyDeletion&& onAnyDeletion,
    OnLinewise&& onLinewise) {
  int lastLine = lines.lastLine();
  bool hasLinesOutside = Forward ? boundary_.hasLinesBelow() : boundary_.hasLinesAbove();
  int endpointLine = VimCore::motionParagraphEndpoint<Forward, LineEdgeType::NextEdge>(
      cursor.line, lines, hasLinesOutside);

  if (endpointLine == VimCore::LINE_OUTSIDE_BOUNDARY) return;

  if constexpr (Forward) {
    if (boundary_.hasSuffix() && endpointLine == lastLine) return;
  } else {
    if (boundary_.hasPrefix() && endpointLine == 0) return;
  }

  for (const auto& spec : specs) {
    SequenceBinding cmd(KeyedSequence::byId(spec.ksId), effortFor(spec.ksId));
    if constexpr (Forward) {
      CursorPos end(endpointLine, 0);
      bool endpointIsEof = endpointLine == lastLine
          && !VimCore::isBlankLineStr(lines[endpointLine]);
      if (endpointIsEof) {
        end.setCol(static_cast<int>(lines[endpointLine].size()));
      }
      if (end <= cursor) continue;

      auto resolved = VimCore::resolveExclusiveDeleteRange(
          CharRange(cursor, end), lines, true);
      TransformExplorerDetail::emitResolvedDeletion(
          resolved, cmd, onAnyDeletion, onLinewise);
    } else {
      CursorPos begin(endpointLine, 0);
      if (begin >= cursor) continue;

      auto resolved = VimCore::resolveExclusiveDeleteRange(
          CharRange(begin, cursor), lines, true);
      TransformExplorerDetail::emitResolvedDeletion(
          resolved, cmd, onAnyDeletion, onLinewise);
    }
  }
}

template<bool Forward, class OnAnyDeletion, class OnLinewise>
void TransformExplorer::exploreSentenceEdits(
    const std::vector<Edit::SentenceEditSpecNoDir>& specs,
    const CursorPos& cursor, const Lines& lines,
    OnAnyDeletion&& onAnyDeletion,
    OnLinewise&& onLinewise) {
  int lastLine = lines.lastLine();
  int boundaryOffset = Forward ? rightColOffset_ : leftColOffset_;
  bool hasLinesOutside = Forward ? boundary_.hasLinesBelow() : boundary_.hasLinesAbove();
  CursorPos endpoint = VimCore::sentenceOperatorEndpoint(
      cursor, lines, Forward, boundaryOffset, hasLinesOutside);

  if (endpoint == POSITION_OUTSIDE_BOUNDARY) return;

  if constexpr (Forward) {
    if (boundary_.hasSuffix() && endpoint.line == lastLine &&
        endpoint.col >= static_cast<int>(lines[lastLine].size()) - rightColOffset_) {
      return;
    }
  } else {
    if (boundary_.hasPrefix() && endpoint.line == 0 && endpoint.col < leftColOffset_) {
      return;
    }
  }

  for (const auto& spec : specs) {
    SequenceBinding cmd(KeyedSequence::byId(spec.ksId), effortFor(spec.ksId));
    if constexpr (Forward) {
      if (endpoint <= cursor) continue;
      CharRange rawRange(cursor, endpoint);
      auto resolved = VimCore::resolveExclusiveDeleteRange(rawRange, lines, true);
      TransformExplorerDetail::emitResolvedDeletion(
          resolved, cmd, onAnyDeletion, onLinewise);
    } else {
      if (endpoint >= cursor) continue;
      CharRange rawRange(endpoint, cursor);
      auto resolved = VimCore::resolveExclusiveDeleteRange(rawRange, lines, true);
      TransformExplorerDetail::emitResolvedDeletion(
          resolved, cmd, onAnyDeletion, onLinewise);
    }
  }
}

template<class OnAnyDeletion, class OnLinewise>
void TransformExplorer::exploreWordEdits(
    const std::vector<Edit::WordEditSpec>& specs,
    const CursorPos& cursor, const Lines& lines,
    OnAnyDeletion&& onAnyDeletion, OnLinewise&& onLinewise) {
  VimCore::WordBoundaryContext boundary = wordBoundaryContext();
  for (const auto& spec : specs) {
    CharRange range = VimCore::wordOperatorRange(
        cursor, lines, spec.target, spec.isBig, boundary);
    if (range.begin == POSITION_OUTSIDE_BOUNDARY ||
        range.end == POSITION_OUTSIDE_BOUNDARY) {
      continue;
    }
    SequenceBinding cmd(KeyedSequence::byId(spec.ksId), effortFor(spec.ksId));
    if (spec.target == VimCore::WordOperatorTarget::DeleteBackToWordBegin) {
      int contentStartCol = cursor.line == 0 ? leftColOffset_ : 0;
      auto resolved = VimCore::resolveBackwardExclusiveWordDeleteRange(
          range.begin, cursor, lines, contentStartCol);
      TransformExplorerDetail::emitResolvedDeletion(
          resolved, cmd, onAnyDeletion, onLinewise);
      continue;
    }
    onAnyDeletion(range, cmd);
  }
}

template<class OnCountedLinewise>
void TransformExplorer::exploreCountedLineEdits(
    const CursorPos& cursor, const Lines& lines, int minCountRepeat,
    OnCountedLinewise&& onCountedLinewise) {
  const int maxCountRepeat = params_.maxPrefixCount;
  int lastLine = lines.lastLine();
  bool hasPrefix = boundary_.hasPrefix();
  bool hasSuffix = boundary_.hasSuffix();

  if (cursor.line + 1 <= lastLine) {
    bool skipDj = (cursor.line == 0 && hasPrefix) ||
                  (cursor.line + 1 == lastLine && hasSuffix);
    if (!skipDj) {
      onCountedLinewise(LineRange(cursor.line, cursor.line + 2),
                        SequenceBinding(KeyedSequence::dj, effortFor(KSId::dj)));
    }
  }

  if (cursor.line - 1 >= 0) {
    bool skipDk = (cursor.line - 1 == 0 && hasPrefix) ||
                  (cursor.line == lastLine && hasSuffix);
    if (!skipDk) {
      onCountedLinewise(LineRange(cursor.line - 1, cursor.line + 1),
                        SequenceBinding(KeyedSequence::dk, effortFor(KSId::dk)));
    }
  }

  if (cursor.line == 0 && hasPrefix) return;

  int lastDeletable = lastLine;
  if (hasSuffix) lastDeletable = lastLine - 1;
  if (lastDeletable < cursor.line) return;

  int maxCount = std::min(lastDeletable - cursor.line + 1, maxCountRepeat);
  for (int n = std::max(2, minCountRepeat); n <= maxCount; n++) {
    RunningEffort countedEffort =
        TransformExplorerDetail::buildCountedEffort<CountClass::EditLine>(
            config_, n, KSId::dd, n);
    onCountedLinewise(LineRange(cursor.line, cursor.line + n),
                      SequenceBinding(KeyedSequence::dd, countedEffort, n));
  }
}

template<class OnCountedJoin>
void TransformExplorer::exploreCountedJoinCommands(
    const CursorPos& cursor, const Lines& lines, int minCountRepeat,
    OnCountedJoin&& onCountedJoin) {
  const int maxCountRepeat = params_.maxPrefixCount;
  int lastLine = lines.lastLine();
  if (cursor.line >= lastLine) return;

  int maxLinesBelow = lastLine - cursor.line;
  if (boundary_.hasLinesBelow() && cursor.line + maxLinesBelow == lastLine) {
    maxLinesBelow--;
  }
  if (boundary_.hasSuffix() && cursor.line + maxLinesBelow == lastLine) {
    maxLinesBelow--;
  }
  if (maxLinesBelow < 1) return;

  int maxCount = std::min(maxLinesBelow + 1, maxCountRepeat);
  for (int count = std::max(2, minCountRepeat); count <= maxCount; count++) {
    RunningEffort jEffort =
        TransformExplorerDetail::buildCountedEffort<CountClass::Join>(
            config_, count, KSId::J, count);
    onCountedJoin(true, SequenceBinding(KeyedSequence::J, jEffort, count));

    RunningEffort gjEffort =
        TransformExplorerDetail::buildCountedEffort<CountClass::Join>(
            config_, count, KSId::gJ, count);
    onCountedJoin(false, SequenceBinding(KeyedSequence::gJ, gjEffort, count));
  }
}

template<class OnDeletion>
void TransformExplorer::exploreCountedWordEdits(
    const CursorPos& cursor, const Lines& lines, int minCountRepeat,
    OnDeletion&& onDeletion) {
  const int maxCountRepeat = params_.maxPrefixCount;
  if (minCountRepeat > maxCountRepeat || maxCountRepeat < 2) return;

  static constexpr int MAX_COUNT_ITERATIONS = 9;
  const int maxIterations = std::min(MAX_COUNT_ITERATIONS, maxCountRepeat);
  VimCore::WordBoundaryContext boundary = wordBoundaryContext();

  auto countedEffortFor = [&](const Edit::WordEditSpec& spec, int count) {
    return spec.isBig
        ? TransformExplorerDetail::buildCountedEffort<CountClass::EditWORD>(
              config_, count, spec.ksId, count)
        : TransformExplorerDetail::buildCountedEffort<CountClass::EditWord>(
              config_, count, spec.ksId, count);
  };

  for (const auto& spec : Edit::FORWARD_WORD_EDITS) {
    CursorPos prev = cursor;
    CursorPos lastEnd;
    int lastCount = 0;
    for (int count = 1; count <= maxIterations; count++) {
      CharRange range = VimCore::wordOperatorRange(
          prev, lines, spec.target, spec.isBig, boundary);
      if (range.begin == POSITION_OUTSIDE_BOUNDARY ||
          range.end == POSITION_OUTSIDE_BOUNDARY ||
          range.end.line != cursor.line) {
        break;
      }
      CursorPos nextPrev = lines.getPrevPos(range.end);
      if (nextPrev == POSITION_OUTSIDE_BOUNDARY || nextPrev == prev) break;
      lastEnd = range.end;
      lastCount = count;
      prev = nextPrev;
    }
    if (lastCount >= minCountRepeat) {
      RunningEffort countedEffort = countedEffortFor(spec, lastCount);
      onDeletion(CharRange(cursor, lastEnd),
                 SequenceBinding(KeyedSequence::byId(spec.ksId), countedEffort, lastCount));
    }
  }

  for (const auto& spec : Edit::BACKWARD_WORD_EDITS) {
    CursorPos prev = cursor;
    CursorPos lastBegin;
    int lastCount = 0;
    for (int count = 1; count <= maxIterations; count++) {
      CharRange range = VimCore::wordOperatorRange(
          prev, lines, spec.target, spec.isBig, boundary);
      if (range.begin == POSITION_OUTSIDE_BOUNDARY ||
          range.end == POSITION_OUTSIDE_BOUNDARY ||
          range.begin == prev || range.begin.line != cursor.line) {
        break;
      }

      lastBegin = range.begin;
      lastCount = count;
      prev = range.begin;
    }
    if (lastCount >= minCountRepeat) {
      CharRange range;
      if (spec.target == VimCore::WordOperatorTarget::DeleteBackToWordBegin) {
        int contentStartCol = cursor.line == 0 ? leftColOffset_ : 0;
        if (cursor.col <= contentStartCol) continue;
        range = VimCore::buildBackwardExclusiveCharRange(
            lastBegin, cursor, lines, contentStartCol);
      } else {
        range = CharRange(lastBegin, VimCore::onePastOnSameLine(lines, cursor));
      }
      RunningEffort countedEffort = countedEffortFor(spec, lastCount);
      onDeletion(range,
                 SequenceBinding(KeyedSequence::byId(spec.ksId), countedEffort, lastCount));
    }
  }
}

template<class OnDeletion>
void TransformExplorer::exploreCountedCharEdits(
    const CursorPos& cursor, const Lines& lines, int contentStart, int contentEnd,
    int minCountRepeat, OnDeletion&& onDeletion) {
  const int maxCountRepeat = params_.maxPrefixCount;
  if (minCountRepeat > maxCountRepeat || maxCountRepeat < 2) return;
  if (contentStart > cursor.col || cursor.col >= contentEnd) return;

  static constexpr int MAX_COUNT_DIGIT = 9;
  int count = std::min(contentEnd - cursor.col, std::min(MAX_COUNT_DIGIT, maxCountRepeat));
  if (count < minCountRepeat) return;

  CharRange range(cursor, CursorPos(cursor.line, cursor.col + count));
  RunningEffort effort =
      TransformExplorerDetail::buildCountedEffort<CountClass::EditChar>(
          config_, count, KSId::x, count);
  onDeletion(range, SequenceBinding(KeyedSequence::x, effort, count));
}

template<class OnJoin>
void TransformExplorer::exploreJoinCommands(
    const CursorPos& cursor, const Lines& lines, OnJoin&& onJoin) {
  if (cursor.line >= lines.lastLine()) return;

  int nextLine = cursor.line + 1;
  if (nextLine == lines.lastLine() && boundary_.hasLinesBelow()) return;

  onJoin(true, SequenceBinding(KeyedSequence::J, effortFor(KSId::J)));
  onJoin(false, SequenceBinding(KeyedSequence::gJ, effortFor(KSId::gJ)));
}

template<class State, class OnAnyDeletion, class OnLinewise, class OnJoin>
void TransformExplorer::exploreAllDeletions(
    const State& state,
    OnAnyDeletion&& onAnyDeletion,
    OnLinewise&& onLinewise,
    OnJoin&& onJoin) {
  const Lines& lines = state.getLines();
  CursorPos cursor = state.getPos();

  auto [contentBegin, contentEnd] = computeTransformBounds(lines, cursor);
  int editContentLen = contentEnd - contentBegin;

  if (editContentLen <= 0) {
    assert(lines[cursor.line].empty());

    exploreFullLineEdits(Edit::EMPTYLINE_FULL_LINE_EDITS, cursor, lines, onLinewise);
    exploreWordEdits(
        Edit::EMPTYLINE_FORWARD_WORD_EDITS, cursor, lines, onAnyDeletion,
        onLinewise);
    exploreWordEdits(
        Edit::EMPTYLINE_BACKWARD_WORD_EDITS, cursor, lines, onAnyDeletion,
        onLinewise);
    exploreJoinCommands(cursor, lines, onJoin);
    return;
  }

  exploreWordEdits(Edit::FORWARD_WORD_EDITS, cursor, lines, onAnyDeletion, onLinewise);
  exploreWordEdits(Edit::BACKWARD_WORD_EDITS, cursor, lines, onAnyDeletion, onLinewise);
  exploreTextObjectEdits(Edit::TEXT_OBJECT_EDITS, cursor, lines, onAnyDeletion);
  exploreHalfLineEdits(Edit::HALF_LINE_EDITS, cursor, lines, contentBegin, contentEnd, onAnyDeletion);
  exploreFullLineEdits(Edit::FULL_LINE_EDITS, cursor, lines, onLinewise);
  exploreCharEdits(cursor, lines, contentBegin, contentEnd, editContentLen, onAnyDeletion);
  exploreParagraphEdits<true>(
      Edit::FORWARD_PARAGRAPH_EDITS, cursor, lines, onAnyDeletion, onLinewise);
  exploreParagraphEdits<false>(
      Edit::BACKWARD_PARAGRAPH_EDITS, cursor, lines, onAnyDeletion, onLinewise);
  exploreSentenceEdits<true>(
      Edit::FORWARD_SENTENCE_EDITS, cursor, lines, onAnyDeletion, onLinewise);
  exploreSentenceEdits<false>(
      Edit::BACKWARD_SENTENCE_EDITS, cursor, lines, onAnyDeletion, onLinewise);
  exploreJoinCommands(cursor, lines, onJoin);
}

// Single-source-of-truth sweep over every per-step explorer enumeration
// the optimizer's main A* loop fires. Both the optimizer and the depth-1
// frontier (TransformFrontier) call this — adding a new explorer
// enumeration here flows to both consumers automatically. If you find
// yourself adding a new `explorer.exploreXxx` call to one consumer,
// it belongs here instead.
//
// Caller is responsible for the boundary-region guards (suffix/prefix
// motion-only branches in the optimizer) and any pre/post-emission
// bookkeeping (cost map, dispatch tracking).
template<class State, class OnDeletion, class OnLinewise, class OnJoin,
         class OnCountedLinewise, class OnCountedJoin>
void sweepExplorerStructurals(
    TransformExplorer& explorer,
    const State& state,
    const Lines& effectiveLines,
    CursorPos cursor,
    int leftColOffset,
    int rightColOffset,
    int minPrefixCount,
    OnDeletion&& onDeletion,
    OnLinewise&& onLinewise,
    OnJoin&& onJoin,
    OnCountedLinewise&& onCountedLinewise,
    OnCountedJoin&& onCountedJoin) {
  explorer.exploreAllDeletions(state, onDeletion, onLinewise, onJoin);
  explorer.exploreCountedLineEdits(cursor, effectiveLines, minPrefixCount, onCountedLinewise);
  explorer.exploreCountedJoinCommands(cursor, effectiveLines, minPrefixCount, onCountedJoin);
  explorer.exploreCountedWordEdits(cursor, effectiveLines, minPrefixCount, onDeletion);

  const int contentStart = (cursor.line == 0) ? leftColOffset : 0;
  int contentEnd = static_cast<int>(effectiveLines[cursor.line].size());
  if (cursor.line == effectiveLines.lastLine() && rightColOffset > 0) {
    contentEnd -= rightColOffset;
  }
  if (contentEnd > contentStart) {
    explorer.exploreCountedCharEdits(cursor, effectiveLines, contentStart, contentEnd,
                                     minPrefixCount, onDeletion);
  }
}
