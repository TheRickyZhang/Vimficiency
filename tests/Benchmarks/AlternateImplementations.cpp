#include "AlternateImplementations.h"
#include "VimCore/VimCore.h"

using namespace std;

namespace Alternate {

// =============================================================================
// Runtime-dispatch version of motionWordCore
// All template parameters converted to runtime conditionals
// =============================================================================

namespace {
// Runtime step helper (no template)
inline Position stepRuntime(const Lines& lines, Position pos, bool forward) {
  if (forward) return lines.getNextPos(pos);
  else return lines.getPrevPos(pos);
}

// Runtime stepBack helper (no template)
inline Position stepBackRuntime(const Lines& lines, Position pos, bool forward) {
  if (forward) return lines.getPrevPos(pos);
  else return lines.getNextPos(pos);
}

// Runtime lineBounded handler (no template, no cold attribute)
Position handleLineBoundedRuntime(Position prev, const Lines& lines, bool forward) {
  if (forward) {
    return Position(prev.line, lines[prev.line].lastCol());
  } else {
    return Position(prev.line, 0);
  }
}
} // anonymous namespace

Position motionWordCoreRuntime(Position pos,
                               const Lines& lines,
                               bool forward,
                               EdgeType edgeType,
                               bool big,
                               bool skipCurrent,
                               bool lineBounded) {
  using namespace VimCore;

  // ---------------------------------------------------------------------------
  // skipCurrent: Move off current position before searching
  // ---------------------------------------------------------------------------
  if (skipCurrent) {
    Position prevPos = pos;
    unsigned char prevChar = lines.get(pos);

    pos = stepRuntime(lines, pos, forward);
    if (pos == prevPos) {
      return POSITION_OUTSIDE_BOUNDARY;
    }

    unsigned char currChar = lines.get(pos);

    // b/B (backward WordEdge): Empty line is a word, stop here
    if (!forward && edgeType == EdgeType::WordEdge) {
      if (currChar == '\n') {
        return pos;
      }
    }

    // ge/gE (backward NextEdge): Line/word boundaries are stopping points
    if (!forward && edgeType == EdgeType::NextEdge) {
      bool crossedLine = (pos.line != prevPos.line);
      bool crossedWordBoundary =
          crossedLine || isBlank(currChar) || isBlank(prevChar) ||
          (!big && isSmallWordChar(currChar) != isSmallWordChar(prevChar));
      if (crossedWordBoundary && !isBlank(currChar)) {
        return pos;
      }
    }
    // e/E (forward WordEdge): No early termination
  }

  unsigned char c = lines.get(pos);

  // Phase 1: If starting on blank, skip to first non-blank
  if (isBlank(c)) {
    do {
      Position prev = pos;
      pos = stepRuntime(lines, pos, forward);
      if (pos == prev) return POSITION_OUTSIDE_BOUNDARY;

      if (lineBounded && pos.line != prev.line) {
        return handleLineBoundedRuntime(prev, lines, forward);
      }

      c = lines.get(pos);
    } while (isBlank(c));

    if (edgeType == EdgeType::NextEdge) return pos;
    if (edgeType == EdgeType::GapEdge) return stepBackRuntime(lines, pos, forward);
  }

  // Phase 2: Skip same-type chars (current word)
  bool startIsWordChar = isSmallWordChar(c);
  bool crossedLine = false;
  do {
    Position prev = pos;
    pos = stepRuntime(lines, pos, forward);
    if (pos == prev) {
      if (edgeType == EdgeType::WordEdge) return prev;
      return POSITION_OUTSIDE_BOUNDARY;
    }
    c = lines.get(pos);
    if (pos.line != prev.line) {
      crossedLine = true;
      break;
    }
  } while (!isBlank(c) && (big || isSmallWordChar(c) == startIsWordChar));

  if (edgeType == EdgeType::WordEdge) return stepBackRuntime(lines, pos, forward);

  // Empty line is a word
  if (crossedLine && c == '\n') {
    if (edgeType == EdgeType::NextEdge) return pos;
    if (edgeType == EdgeType::GapEdge) return stepBackRuntime(lines, pos, forward);
  }

  // Phase 3: If at non-blank different type (word only, not WORD)
  if (!isBlank(c)) {
    if (edgeType == EdgeType::NextEdge) return pos;
    if (edgeType == EdgeType::GapEdge) return stepBackRuntime(lines, pos, forward);
  }

  // Phase 4: Skip whitespace to reach next word (but stop at empty lines)
  while (isWhitespace(c)) {
    Position prev = pos;
    pos = stepRuntime(lines, pos, forward);
    if (pos == prev) return POSITION_OUTSIDE_BOUNDARY;

    if (lineBounded && pos.line != prev.line) {
      return handleLineBoundedRuntime(prev, lines, forward);
    }

    c = lines.get(pos);
    if (c == '\n') {
      if (edgeType == EdgeType::NextEdge) return pos;
      if (edgeType == EdgeType::GapEdge) return stepBackRuntime(lines, pos, forward);
    }
  }

  if (edgeType == EdgeType::GapEdge) return stepBackRuntime(lines, pos, forward);
  return pos;
}

// =============================================================================
// Runtime-dispatch version of motionWordEndpoint
// =============================================================================

Position motionWordEndpointRuntime(Position cursor,
                                   const Lines& lines,
                                   bool forward,
                                   EdgeType edgeType,
                                   bool big,
                                   bool skipCurrent,
                                   int boundaryOffset,
                                   bool hasLinesOutside,
                                   bool lineBounded) {
  Position endpoint = motionWordCoreRuntime(cursor, lines, forward, edgeType,
                                            big, skipCurrent, lineBounded);

  if (endpoint == POSITION_OUTSIDE_BOUNDARY) {
    return POSITION_OUTSIDE_BOUNDARY;
  }

  // Boundary checking
  int lastLine = lines.lastLine();
  if (forward) {
    // Check suffix boundary
    if (hasLinesOutside && endpoint.line > lastLine) {
      return POSITION_OUTSIDE_BOUNDARY;
    }
    if (boundaryOffset > 0 && endpoint.line == lastLine) {
      int lineLen = static_cast<int>(lines[lastLine].size());
      if (endpoint.col >= lineLen - boundaryOffset) {
        return POSITION_OUTSIDE_BOUNDARY;
      }
    }
  } else {
    // Check prefix boundary
    if (hasLinesOutside && endpoint.line < 0) {
      return POSITION_OUTSIDE_BOUNDARY;
    }
    if (boundaryOffset > 0 && endpoint.line == 0) {
      if (endpoint.col < boundaryOffset) {
        return POSITION_OUTSIDE_BOUNDARY;
      }
    }
  }

  return endpoint;
}

} // namespace Alternate
