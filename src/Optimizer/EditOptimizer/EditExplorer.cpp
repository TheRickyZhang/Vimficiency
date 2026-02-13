#include "EditExplorer.h"
#include "EditSearchContext.h"
#include "EditToSpec.h"
#include "Boundary/EditBoundary.h"
#include "VimCore/VimCore.h"
#include "VimCore/VimEndpointUtils.h"
#include "VimCore/VimMotionUtils.h"

#include <algorithm>

using namespace std;

EditExplorer::EditExplorer(EditSearchContext& ctx) : ctx_(ctx) {}

bool EditExplorer::inBoundaryRegion(const Position& pos, const Lines& lines) const {
  return ctx_.inBoundaryRegion(pos, lines);
}

pair<int, int> EditExplorer::computeEditBounds(const Lines& lines, const Position& cursor) const {
  return ctx_.computeEditBounds(lines, cursor);
}

// =============================================================================
// Exploration Helper Methods
// =============================================================================

void EditExplorer::exploreTextObjectEdits(
    const vector<Edit::TextObjectEditSpec>& specs,
    const Position& cursor, const Lines& lines, DeletionCallback onDeletion) {
  for (const auto& spec : specs) {
    Range range = VimCore::textObjectRange(
        cursor, lines, spec.isInner, spec.isBig,
        ctx_.leftColOffset, ctx_.rightColOffset,
        ctx_.editBoundary.hasLinesAbove(), ctx_.editBoundary.hasLinesBelow());

    if (range.first == POSITION_OUTSIDE_BOUNDARY || range.last == POSITION_OUTSIDE_BOUNDARY)
      continue;

    onDeletion(range, spec.ks);
  }
}

void EditExplorer::exploreHalfLineEdits(
    const vector<Edit::LineEditSpec>& specs,
    const Position& cursor, const Lines& lines, int contentStart, int contentEnd,
    DeletionCallback onDeletion) {
  int lastEditLine = lines.lastLine();

  for (const Edit::LineEditSpec& spec : specs) {
    if (spec.ks.seq.keys == "D") {
      if (cursor.line == lastEditLine && ctx_.editBoundary.hasSuffix()) continue;

      int lineLen = static_cast<int>(lines[cursor.line].size());
      int lineContentEnd = lineLen;
      if (cursor.line == lastEditLine && ctx_.rightColOffset > 0) {
        lineContentEnd -= ctx_.rightColOffset;
      }
      if (lineContentEnd <= 0) continue;

      int endCol = lineContentEnd - 1;
      if (endCol < cursor.col) continue;
      Range range(cursor, Position(cursor.line, endCol));
      onDeletion(range, spec.ks);
    } else if (spec.ks.seq.keys == "d0") {
      if (cursor.line == 0 && ctx_.editBoundary.hasPrefix()) continue;

      int lineContentStart = (cursor.line == 0) ? ctx_.leftColOffset : 0;
      if (cursor.col <= lineContentStart) continue;
      Range range(Position(cursor.line, lineContentStart),
                  Position(cursor.line, cursor.col - 1));
      onDeletion(range, spec.ks);
    } else {
      assert(false && "Unexpected half-line edit command");
    }
  }
}

void EditExplorer::exploreFullLineEdits(
    const std::vector<Edit::FullLineEditSpec>& specs,
    const Position& cursor, const Lines& lines, LinewiseCallback onLinewise) {
  if ((cursor.line == 0 && ctx_.editBoundary.hasPrefix()) ||
      (cursor.line == lines.lastLine() && ctx_.editBoundary.hasSuffix())) {
    return;
  }
  for (const auto& spec : specs) {
    onLinewise(cursor.line, spec.ks);
  }
}

void EditExplorer::exploreCharEdits(
    const Position& cursor, const Lines& lines, int contentStart, int contentEnd,
    int editContentLen, DeletionCallback onDeletion) {
  static const KeyedSequence xKS("x", {Key::Key_X});
  static const KeyedSequence XKS("X", {Key::Key_Shift, Key::Key_X});

  // x: delete char at cursor (must be in content region)
  if (contentStart <= cursor.col && cursor.col < contentEnd) {
    onDeletion(Range(cursor, cursor), xKS);
  }

  // X: delete char before cursor
  if (cursor.col > contentStart && cursor.col <= contentEnd) {
    Position before(cursor.line, cursor.col - 1);
    onDeletion(Range(before, before), XKS);
  }
}

// Templated helper for paragraph edit exploration
template<bool Forward>
void EditExplorer::exploreParagraphEdits(
    const std::vector<Edit::ParagraphEditSpecNoDir>& specs,
    const Position& cursor, const Lines& lines,
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
      // d}: characterwise delete from cursor through the paragraph end.
      // At EOF (endpoint is last line, not blank), d} deletes through endpoint.
      // At a blank separator, d} deletes up to (not including) the blank line.
      // The linewise case (cursor at col 0) is already covered by dd.
      bool endpointIsBlank = VimCore::isBlankLineStr(lines[endpointLine]);
      int endLine = endpointIsBlank ? endpointLine - 1 : endpointLine;
      if (endLine < cursor.line) continue;
      int endCol = std::max(0, static_cast<int>(lines[endLine].size()) - 1);
      Range r(cursor, Position(endLine, endCol));
      onDeletion(r, spec.ks);
    } else {
      // d{: when endpointLine == cursor.line, { stays on the same line and
      // d{ is characterwise exclusive (not linewise), so skip.
      // Multi-line d{ (endpointLine < cursor.line) not yet modeled.
      if (endpointLine >= cursor.line) continue;
    }
  }
}

// Templated helper for sentence edit exploration
template<bool Forward>
void EditExplorer::exploreSentenceEdits(
    const std::vector<Edit::SentenceEditSpecNoDir>& specs,
    const Position& cursor, const Lines& lines, DeletionCallback onDeletion) {
  int lastLine = lines.lastLine();

  int boundaryOffset = Forward ? ctx_.rightColOffset : ctx_.leftColOffset;
  bool hasLinesOutside = Forward ? ctx_.editBoundary.hasLinesBelow() : ctx_.editBoundary.hasLinesAbove();
  Position endpoint = VimCore::motionSentenceEndpoint<Forward, SentenceEdgeType::NextEdge>(
      cursor, lines, boundaryOffset, hasLinesOutside);

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
      // d): delete from cursor to just before endpoint (exclusive)
      if (endpoint <= cursor) continue;
      Position goalPos = lines.getPrevPos(endpoint);
      if (goalPos == POSITION_OUTSIDE_BOUNDARY) continue;
      onDeletion(Range(cursor, goalPos), spec.ks);
    } else {
      // d(: delete from endpoint to just before cursor (exclusive)
      if (endpoint >= cursor) continue;

      // Skip when cursor is on trailing whitespace at EOL and endpoint is col 0
      if (endpoint.col == 0 && cursor.line > endpoint.line) {
        bool cursorOnTrailingWs = false;
        if (cursor.col > 0 && cursor.col < static_cast<int>(lines[cursor.line].size())) {
          char cursorChar = lines[cursor.line][cursor.col];
          if (cursorChar == ' ' || cursorChar == '\t') {
            cursorOnTrailingWs = true;
            for (int c = cursor.col + 1; c < static_cast<int>(lines[cursor.line].size()); c++) {
              char ch = lines[cursor.line][c];
              if (ch != ' ' && ch != '\t') {
                cursorOnTrailingWs = false;
                break;
              }
            }
          }
        }
        if (cursorOnTrailingWs) continue;
      }

      Position goalPos = Position(cursor.line, cursor.col - 1);
      if (cursor.col == 0) {
        if (cursor.line == 0) continue;
        goalPos = Position(cursor.line - 1, lines[cursor.line - 1].lastCol());
      }
      onDeletion(Range(endpoint, goalPos), spec.ks);
    }
  }
}

// =============================================================================
// Templated Exploration Helpers - EdgeType known at compile time
// =============================================================================

template<EdgeType Edge>
void EditExplorer::exploreForwardWordEdits(
    const vector<Edit::ForwardWordEditSpecNoEdge>& specs,
    const Position& cursor, const Lines& lines, DeletionCallback onDeletion) {
  for (const auto& spec : specs) {
    Position endpoint = VimCore::motionWordEndpoint<true, Edge>(
        cursor, lines, spec.isBig, spec.skipCurrent,
        ctx_.rightColOffset, ctx_.editBoundary.hasLinesBelow(), /*lineBounded=*/false);

    if (endpoint == POSITION_OUTSIDE_BOUNDARY || endpoint == cursor)
      continue;

    // Special case for dw/dW (GapEdge): Vim does NOT cross lines.
    if constexpr (Edge == EdgeType::GapEdge) {
      if (endpoint.line > cursor.line) {
        Position wordEnd = VimCore::motionWordEndpoint<true, EdgeType::WordEdge>(
            cursor, lines, spec.isBig, spec.skipCurrent,
            ctx_.rightColOffset, ctx_.editBoundary.hasLinesBelow(), /*lineBounded=*/false);
        if (wordEnd == POSITION_OUTSIDE_BOUNDARY || wordEnd == cursor)
          continue;
        endpoint = wordEnd;
      }
    }

    onDeletion(Range(cursor, endpoint), spec.ks);
  }
}

template<EdgeType Edge>
void EditExplorer::exploreBackwardWordEdits(
    const vector<Edit::BackwardWordEditSpecNoEdge>& specs,
    const Position& cursor, const Lines& lines, DeletionCallback onDeletion) {
  for (const auto& spec : specs) {
    // For inclusive backward deletes (dge/dgE), check cursor first
    if (!spec.isExclusiveAtCursor && inBoundaryRegion(cursor, lines))
      continue;

    Position endpoint = VimCore::motionWordEndpoint<false, Edge>(
        cursor, lines, spec.isBig, spec.skipCurrent,
        ctx_.leftColOffset, ctx_.editBoundary.hasLinesAbove(), /*lineBounded=*/false);

    if (endpoint == POSITION_OUTSIDE_BOUNDARY || endpoint == cursor)
      continue;

    Range range;
    if (spec.isExclusiveAtCursor) {
      int cursorContentCol = cursor.col - (cursor.line == 0 ? ctx_.leftColOffset : 0);
      if (cursorContentCol > 0) {
        range = Range(endpoint, Position(cursor.line, cursor.col - 1));
      } else if (endpoint.line < cursor.line) {
        int prevLine = cursor.line - 1;
        range = Range(endpoint, Position(prevLine, lines[prevLine].lastCol()));
      } else {
        continue;
      }
    } else {
      range = Range(endpoint, cursor);
    }

    onDeletion(range, spec.ks);
  }
}

// Explicit template instantiations
template void EditExplorer::exploreForwardWordEdits<EdgeType::WordEdge>(
    const vector<Edit::ForwardWordEditSpecNoEdge>&, const Position&, const Lines&, DeletionCallback);
template void EditExplorer::exploreForwardWordEdits<EdgeType::GapEdge>(
    const vector<Edit::ForwardWordEditSpecNoEdge>&, const Position&, const Lines&, DeletionCallback);
template void EditExplorer::exploreBackwardWordEdits<EdgeType::WordEdge>(
    const vector<Edit::BackwardWordEditSpecNoEdge>&, const Position&, const Lines&, DeletionCallback);
template void EditExplorer::exploreBackwardWordEdits<EdgeType::NextEdge>(
    const vector<Edit::BackwardWordEditSpecNoEdge>&, const Position&, const Lines&, DeletionCallback);
template void EditExplorer::exploreParagraphEdits<true>(
    const vector<Edit::ParagraphEditSpecNoDir>&, const Position&, const Lines&, DeletionCallback, LinewiseCallback);
template void EditExplorer::exploreParagraphEdits<false>(
    const vector<Edit::ParagraphEditSpecNoDir>&, const Position&, const Lines&, DeletionCallback, LinewiseCallback);
template void EditExplorer::exploreSentenceEdits<true>(
    const vector<Edit::SentenceEditSpecNoDir>&, const Position&, const Lines&, DeletionCallback);
template void EditExplorer::exploreSentenceEdits<false>(
    const vector<Edit::SentenceEditSpecNoDir>&, const Position&, const Lines&, DeletionCallback);

// =============================================================================
// Counted Exploration Methods
// =============================================================================

void EditExplorer::exploreCountedLineEdits(
    const Position& cursor, const Lines& lines,
    int minCountRepeat, CountedLinewiseCallback onCountedLinewise) {
  if (!onCountedLinewise) return;

  int lastLine = lines.lastLine();
  bool hasPrefix = ctx_.editBoundary.hasPrefix();
  bool hasSuffix = ctx_.editBoundary.hasSuffix();

  // dj: 2-line delete downward (always explored, it's 2 keystrokes not a counted command)
  if (cursor.line + 1 <= lastLine) {
    bool skipDj = (cursor.line == 0 && hasPrefix) ||
                  (cursor.line + 1 == lastLine && hasSuffix);
    if (!skipDj) {
      static const KeyedSequence djKS("dj", {Key::Key_D, Key::Key_J});
      onCountedLinewise(LineRange(cursor.line, cursor.line + 1), djKS);
    }
  }

  // dk: 2-line delete upward (always explored, it's 2 keystrokes not a counted command)
  if (cursor.line - 1 >= 0) {
    bool skipDk = (cursor.line - 1 == 0 && hasPrefix) ||
                  (cursor.line == lastLine && hasSuffix);
    if (!skipDk) {
      static const KeyedSequence dkKS("dk", {Key::Key_D, Key::Key_K});
      onCountedLinewise(LineRange(cursor.line - 1, cursor.line), dkKS);
    }
  }

  // {n}dd: counted line delete (n >= minCountRepeat)
  if (cursor.line == 0 && hasPrefix) return;

  // Find last deletable line (respecting suffix boundary)
  int lastDeletable = lastLine;
  if (hasSuffix) lastDeletable = lastLine - 1;
  if (lastDeletable < cursor.line) return;

  int maxCount = lastDeletable - cursor.line + 1;
  static const KeyedSequence ddKS("dd", {Key::Key_D, Key::Key_D});

  for (int n = max(2, minCountRepeat); n <= maxCount; n++) {
    KeyedSequence countedKS;
    countedKS.appendCounted(n, ddKS);
    onCountedLinewise(LineRange(cursor.line, cursor.line + n - 1), countedKS);
  }
}

void EditExplorer::exploreCountedJoinCommands(
    const Position& cursor, const Lines& lines,
    int minCountRepeat, CountedJoinCallback onCountedJoin) {
  if (!onCountedJoin) return;

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

  static const KeyedSequence JKS("J", {Key::Key_Shift, Key::Key_J});
  static const KeyedSequence gJKS("gJ", {Key::Key_G, Key::Key_Shift, Key::Key_J});

  // count means "join count lines" (current + count-1 below)
  // so count=2 joins current + 1 below, count=3 joins current + 2 below, etc.
  for (int count = max(2, minCountRepeat); count <= maxLinesBelow + 1; count++) {
    KeyedSequence countedJKS;
    countedJKS.appendCounted(count, JKS);
    KeyedSequence countedGjKS;
    countedGjKS.appendCounted(count, gJKS);
    onCountedJoin(count, true, countedJKS);
    onCountedJoin(count, false, countedGjKS);
  }
}

void EditExplorer::exploreCountedWordEdits(
    const Position& cursor, const Lines& lines,
    int minCountRepeat, DeletionCallback onDeletion) {
  if (!onDeletion) return;
  if (minCountRepeat < 2) return;

  static constexpr int MAX_COUNT_ITERATIONS = 9;

  // Use raw motion functions (motionE, motionW, motionB, motionGe) to match
  // Edit::applyEdit exactly. The boundary-aware motionWordEndpoint can diverge
  // from applyEdit after A* deletions change the buffer structure.

  // Forward word-end edits: {n}de, {n}dE
  for (const auto& spec : Edit::FORWARD_WORDEDGE_EDITS) {
    Position prev = cursor;
    for (int count = 1; count <= MAX_COUNT_ITERATIONS; count++) {
      Position endpoint = prev;
      VimCore::motionE(endpoint, lines, spec.isBig);

      if (endpoint == prev) break;
      if (endpoint.line != cursor.line) break;

      // Handle past-end position (clamp to last valid char)
      int lineLen = static_cast<int>(lines[endpoint.line].size());
      if (endpoint.col >= lineLen) endpoint.setCol(lineLen - 1);

      if (inBoundaryRegion(endpoint, lines)) break;

      if (count >= minCountRepeat) {
        KeyedSequence countedKS;
        countedKS.appendCounted(count, spec.ks);
        onDeletion(Range(cursor, endpoint), countedKS);
      }

      prev = endpoint;
    }
  }

  // Forward word-gap edits: {n}dw, {n}dW
  for (const auto& spec : Edit::FORWARD_GAPEDGE_EDITS) {
    Position prev = cursor;
    for (int count = 1; count <= MAX_COUNT_ITERATIONS; count++) {
      Position endpoint = prev;
      VimCore::motionW(endpoint, lines, spec.isBig);

      if (endpoint == prev) break;
      if (endpoint.line != cursor.line) break;

      // motionW returns exclusive end (start of next word).
      // Convert to inclusive: [cursor, endpoint-1]
      int lineLen = static_cast<int>(lines[endpoint.line].size());
      Position inclusiveEnd;
      if (endpoint.col >= lineLen) {
        // Past end — delete to last char (matches deleteToNextWord)
        inclusiveEnd = Position(endpoint.line, lineLen - 1);
      } else {
        inclusiveEnd = Position(endpoint.line, endpoint.col - 1);
      }

      if (inBoundaryRegion(inclusiveEnd, lines)) break;

      if (count >= minCountRepeat) {
        KeyedSequence countedKS;
        countedKS.appendCounted(count, spec.ks);
        onDeletion(Range(cursor, inclusiveEnd), countedKS);
      }

      prev = endpoint;  // Next iteration starts from motionW's exclusive position
    }
  }

  // Backward word edits: {n}db, {n}dB
  for (const auto& spec : Edit::BACKWARD_WORDEDGE_EDITS) {
    Position prev = cursor;
    for (int count = 1; count <= MAX_COUNT_ITERATIONS; count++) {
      Position endpoint = prev;
      VimCore::motionB(endpoint, lines, spec.isBig);

      if (endpoint == prev) break;
      if (endpoint.line != cursor.line) break;

      if (inBoundaryRegion(endpoint, lines)) break;

      if (count >= minCountRepeat) {
        // db/dB are exclusive at cursor: range is [endpoint, cursor-1]
        if (cursor.col <= (cursor.line == 0 ? ctx_.leftColOffset : 0)) break;
        Range range(endpoint, Position(cursor.line, cursor.col - 1));

        KeyedSequence countedKS;
        countedKS.appendCounted(count, spec.ks);
        onDeletion(range, countedKS);
      }

      prev = endpoint;
    }
  }

  // Backward word-end edits: {n}dge, {n}dgE
  for (const auto& spec : Edit::BACKWARD_NEXTEDGE_EDITS) {
    if (!spec.isExclusiveAtCursor && inBoundaryRegion(cursor, lines))
      continue;

    Position prev = cursor;
    for (int count = 1; count <= MAX_COUNT_ITERATIONS; count++) {
      Position endpoint = prev;
      VimCore::motionGe(endpoint, lines, spec.isBig);

      if (endpoint == prev) break;
      if (endpoint.line != cursor.line) break;

      if (inBoundaryRegion(endpoint, lines)) break;

      if (count >= minCountRepeat) {
        KeyedSequence countedKS;
        countedKS.appendCounted(count, spec.ks);
        onDeletion(Range(endpoint, cursor), countedKS);
      }

      prev = endpoint;
    }
  }
}

// =============================================================================
// Main Exploration Entry Point
// =============================================================================

void EditExplorer::exploreJoinCommands(
    const Position& cursor, const Lines& lines, JoinCallback onJoin) {
  if (!onJoin) return;

  // J/gJ require a next line to join with
  if (cursor.line >= lines.lastLine()) return;

  int nextLine = cursor.line + 1;

  // Don't allow J if there are lines below the edit region
  if (nextLine == lines.lastLine() && ctx_.editBoundary.hasLinesBelow()) return;

  // Explore both J (with space) and gJ (without space)
  static const KeyedSequence JKS("J", {Key::Key_Shift, Key::Key_J});
  static const KeyedSequence gJKS("gJ", {Key::Key_G, Key::Key_Shift, Key::Key_J});
  onJoin(true, JKS);
  onJoin(false, gJKS);
}

void EditExplorer::exploreAllDeletions(const EditState& state,
                                       DeletionCallback onDeletion,
                                       LinewiseCallback onLinewise,
                                       MotionCallback onMotion,
                                       JoinCallback onJoin) {
  const Lines& lines = state.getLines();
  Position cursor = state.getPos();

  // Right boundary (suffix region): cursor on last line, in suffix columns
  if (cursor.line == lines.lastLine() && ctx_.rightColOffset > 0 &&
      cursor.col + ctx_.rightColOffset >= static_cast<int>(lines.getSize(cursor.line))) {
    // Can still do backward deletions that don't touch suffix
    exploreBackwardWordEdits<EdgeType::WordEdge>(Edit::BACKWARD_WORDEDGE_EDITS, cursor, lines, onDeletion);

    if (onMotion) {
      // h: move left within line (away from suffix)
      if (cursor.col > 0) { onMotion(Position(cursor.line, cursor.col - 1), KeyedSequence::h); }
      // k: move up to previous line (escape suffix line entirely)
      if (cursor.line > 0) {
        int newCol = min(cursor.targetCol, lines[cursor.line - 1].lastCol());
        onMotion(Position(cursor.line - 1, newCol, cursor.targetCol), KeyedSequence::k);
      }
    }
    return;
  }

  // Left boundary (prefix region): cursor on line 0, in prefix columns
  if (cursor.line == 0 && cursor.col < ctx_.leftColOffset) {
    if (onMotion) {
      // l: move right within line (away from prefix)
      if (cursor.col < static_cast<int>(lines[0].size()) - 1) {
        onMotion(Position(0, cursor.col + 1), KeyedSequence::l);
      }
      // j: move down to next line (escape prefix line entirely)
      if (lines.lastLine() > 0) {
        int newCol = min(cursor.targetCol, lines[1].lastCol());
        onMotion(Position(1, newCol, cursor.targetCol), KeyedSequence::j);
      }
    }
    return;
  }

  auto [contentBegin, contentEnd] = computeEditBounds(lines, cursor);
  int editContentLen = contentEnd - contentBegin;

  // Empty editable content: either a truly empty line, or a line that is
  // entirely prefix/suffix with no editable region. Explore limited set.
  if (editContentLen <= 0) {
    assert(lines[cursor.line].empty() || ctx_.inBoundaryRegion(cursor, lines));

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
  exploreSentenceEdits<true>(Edit::FORWARD_SENTENCE_EDITS, cursor, lines, onDeletion);
  exploreSentenceEdits<false>(Edit::BACKWARD_SENTENCE_EDITS, cursor, lines, onDeletion);
  exploreJoinCommands(cursor, lines, onJoin);
}
