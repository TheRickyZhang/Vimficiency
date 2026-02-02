#include "EditExplorer.h"
#include "EditSearchContext.h"
#include "Boundary/EditBoundary.h"
#include "Keyboard/EditToKeys.h"
#include "Keyboard/MotionToKeysPrimitives.h"
#include "VimCore/VimEndpointUtils.h"

#include <cstring>

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

    onDeletion(range, spec.cmd, spec.keys);
  }
}

void EditExplorer::exploreHalfLineEdits(
    const vector<Edit::LineEditSpec>& specs,
    const Position& cursor, const Lines& lines, int contentStart, int contentEnd,
    DeletionCallback onDeletion) {
  int lastEditLine = lines.lastLine();

  for (const Edit::LineEditSpec& spec : specs) {
    if (!strcmp(spec.cmd, "D")) {
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
      onDeletion(range, spec.cmd, spec.keys);
    } else if (!strcmp(spec.cmd, "d0")) {
      if (cursor.line == 0 && ctx_.editBoundary.hasPrefix()) continue;

      int lineContentStart = (cursor.line == 0) ? ctx_.leftColOffset : 0;
      if (cursor.col <= lineContentStart) continue;
      Range range(Position(cursor.line, lineContentStart),
                  Position(cursor.line, cursor.col - 1));
      onDeletion(range, spec.cmd, spec.keys);
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
    onLinewise(cursor.line, spec.cmd, spec.keys);
  }
}

void EditExplorer::exploreCharEdits(
    const Position& cursor, const Lines& lines, int contentStart, int contentEnd,
    int editContentLen, DeletionCallback onDeletion) {
  // x: delete char at cursor (must be in content region)
  if (contentStart <= cursor.col && cursor.col < contentEnd) {
    onDeletion(Range(cursor, cursor), "x", Deletion::CHAR.at("x"));
  }

  // X: delete char before cursor
  if (cursor.col > contentStart && cursor.col <= contentEnd) {
    Position before(cursor.line, cursor.col - 1);
    onDeletion(Range(before, before), "X", Deletion::CHAR.at("X"));
  }
}

// Templated helper for paragraph edit exploration
template<bool Forward>
void EditExplorer::exploreParagraphEdits(
    const std::vector<Edit::ParagraphEditSpecNoDir>& specs,
    const Position& cursor, const Lines& lines, LinewiseCallback onLinewise) {
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
      // d}: delete from cursor line to line before endpoint (endpoint is blank line)
      int endLine = endpointLine - 1;
      if (endLine < cursor.line) continue;
      if (endLine == cursor.line) {
        onLinewise(cursor.line, spec.cmd, spec.keys);
      }
    } else {
      // d{: delete from endpoint line to cursor line (inclusive)
      if (endpointLine > cursor.line) continue;
      if (endpointLine == cursor.line) {
        onLinewise(cursor.line, spec.cmd, spec.keys);
      }
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
      onDeletion(Range(cursor, goalPos), spec.cmd, spec.keys);
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
      onDeletion(Range(endpoint, goalPos), spec.cmd, spec.keys);
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

    onDeletion(Range(cursor, endpoint), spec.cmd, spec.keys);
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

    onDeletion(range, spec.cmd, spec.keys);
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
    const vector<Edit::ParagraphEditSpecNoDir>&, const Position&, const Lines&, LinewiseCallback);
template void EditExplorer::exploreParagraphEdits<false>(
    const vector<Edit::ParagraphEditSpecNoDir>&, const Position&, const Lines&, LinewiseCallback);
template void EditExplorer::exploreSentenceEdits<true>(
    const vector<Edit::SentenceEditSpecNoDir>&, const Position&, const Lines&, DeletionCallback);
template void EditExplorer::exploreSentenceEdits<false>(
    const vector<Edit::SentenceEditSpecNoDir>&, const Position&, const Lines&, DeletionCallback);

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
  for (const auto& [cmd, keys] : Deletion::JOIN) {
    bool addSpace = (cmd == "J");
    onJoin(addSpace, cmd.c_str(), keys);
  }
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
      if (cursor.col > 0) { onMotion(Position(cursor.line, cursor.col - 1), "h", hjkl.at("h")); }
      // k: move up to previous line (escape suffix line entirely)
      if (cursor.line > 0) {
        int newCol = min(cursor.targetCol, lines[cursor.line - 1].lastCol());
        onMotion(Position(cursor.line - 1, newCol, cursor.targetCol), "k", hjkl.at("k"));
      }
    }
    return;
  }

  // Left boundary (prefix region): cursor on line 0, in prefix columns
  if (cursor.line == 0 && cursor.col < ctx_.leftColOffset) {
    if (onMotion) {
      // l: move right within line (away from prefix)
      if (cursor.col < static_cast<int>(lines[0].size()) - 1) {
        onMotion(Position(0, cursor.col + 1), "l", hjkl.at("l"));
      }
      // j: move down to next line (escape prefix line entirely)
      if (lines.lastLine() > 0) {
        int newCol = min(cursor.targetCol, lines[1].lastCol());
        onMotion(Position(1, newCol, cursor.targetCol), "j", hjkl.at("j"));
      }
    }
    return;
  }

  auto [contentBegin, contentEnd] = computeEditBounds(lines, cursor);
  int editContentLen = contentEnd - contentBegin;

  // Empty line: explore limited set
  if (editContentLen <= 0) {
    assert(lines[cursor.line].size() == 0);

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
  exploreParagraphEdits<true>(Edit::FORWARD_PARAGRAPH_EDITS, cursor, lines, onLinewise);
  exploreParagraphEdits<false>(Edit::BACKWARD_PARAGRAPH_EDITS, cursor, lines, onLinewise);
  exploreSentenceEdits<true>(Edit::FORWARD_SENTENCE_EDITS, cursor, lines, onDeletion);
  exploreSentenceEdits<false>(Edit::BACKWARD_SENTENCE_EDITS, cursor, lines, onDeletion);
  exploreJoinCommands(cursor, lines, onJoin);
}
