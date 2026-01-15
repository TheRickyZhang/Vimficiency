#include "Boundary.h"
#include "VimCore/EndpointType.h"
#include "VimCore/VimMovementUtils.h"
#include <stdexcept>

// =============================================================================
// Helper: Get char type at position
// =============================================================================
//
// Precondition: position is valid (lines non-empty, line non-empty, col in
// range) Exception: returns Newline for boundary positions at line edges

CharType getCharTypeAt(const Lines &lines, Position pos) {
  if (lines.empty() || lines[pos.line].empty())
    return CharType::Newline;
  return getCharType(lines[pos.line][pos.col]);
}

CharType getCharTypeBefore(const Lines &lines, Position pos) {
  if (pos.col > 0) {
    return getCharType(lines[pos.line][pos.col - 1]);
  }
  // At column 0: go to previous line's last char
  for (int prevLine = pos.line - 1; prevLine >= 0; --prevLine) {
    if (!lines[prevLine].empty()) {
      return getCharType(lines[prevLine].back());
    }
  }
  return CharType::Newline;
}

CharType getCharTypeAfter(const Lines &lines, Position pos) {
  const std::string &line = lines[pos.line];
  if (pos.col + 1 < static_cast<int>(line.size())) {
    return getCharType(line[pos.col + 1]);
  }
  // At end of line: go to next line's first char
  for (int nextLine = pos.line + 1; nextLine < static_cast<int>(lines.size());
       ++nextLine) {
    if (!lines[nextLine].empty()) {
      return getCharType(lines[nextLine][0]);
    }
  }
  return CharType::Newline;
}

// =============================================================================
// Core API
// =============================================================================

bool extendsTooFar(const Lines &lines, Position cursor, Position boundaryPos,
                   const MotionInfo &info) {

  Position endPos = cursor;

  // TODO: Custom implementation that can break early for efficiency
  VimMovementUtils::motionWord(
    endPos,
    lines,
    info.isForward,
    info.endpointType,
    info.isWORD
  );

  if (info.isForward) {
    return endPos >= boundaryPos;
  } else {
    return endPos <= boundaryPos;
  }
}
