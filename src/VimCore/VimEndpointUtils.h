#pragma once

#include "Types/LineEdgeType.h"
#include "Types/CharRange.h"
#include "Types/LineRange.h"
#include "Types/Lines.h"

// Forward declaration
struct TransformBoundary;

namespace VimCore {

// =============================================================================
// Endpoint and CharRange Computation for Boundary Checking
// =============================================================================
//
// These functions return positions/ranges without modifying state.
// They are used during A* search to predict whether motions cross boundaries.
//
// See boundary-logic.md for the crossing table model.

// =============================================================================
// Word Endpoint/CharRange Computation
// =============================================================================

enum class WordMotionTarget {
  NextBegin,
  NextEnd,
  PreviousBegin,
  PreviousEnd,
};

enum class WordOperatorTarget {
  DeleteToNextWord,
  DeleteToWordEnd,
  DeleteBackToWordBegin,
  DeleteBackToWordEnd,
};

enum class WordTextObjectKind {
  Inner,
  Around,
};

struct WordBoundaryContext {
  int leftColOffset = 0;
  int rightColOffset = 0;
  bool hasLinesAbove = false;
  bool hasLinesBelow = false;
  bool clampOutside = true;

  // First valid content column for a given line within the effective region.
  // The prefix offset only applies to line 0; other lines have no prefix.
  int contentStartCol(int line) const {
    return line == 0 ? leftColOffset : 0;
  }

  // Effective end-of-content column on `line` (exclusive). On the last edit
  // line, the suffix offset is excluded; other lines use the full line size.
  int effectiveLineEnd(const Lines& lines, int line, bool isLastEditLine) const {
    int lineSize = static_cast<int>(lines[line].size());
    return isLastEditLine ? lineSize - rightColOffset : lineSize;
  }

  // True if `pos` falls inside the protected suffix region on the last edit
  // line. Returns false when there is no suffix (rightColOffset == 0).
  bool inSuffixRegion(const CursorPos& pos, const Lines& lines) const {
    if (rightColOffset <= 0) return false;
    if (pos.line != lines.lastLine()) return false;
    return pos.col >= static_cast<int>(lines[pos.line].size()) - rightColOffset;
  }
};

CursorPos wordMotionEndpoint(CursorPos cursor,
                             const Lines& lines,
                             WordMotionTarget target,
                             bool isBigWord,
                             WordBoundaryContext boundary = {});

CharRange wordOperatorRange(CursorPos cursor,
                            const Lines& lines,
                            WordOperatorTarget target,
                            bool isBigWord,
                            WordBoundaryContext boundary = {});

CharRange wordTextObjectRange(CursorPos cursor,
                              const Lines& lines,
                              WordTextObjectKind kind,
                              bool isBigWord,
                              WordBoundaryContext boundary = {});

CharRange quoteTextObjectRange(CursorPos cursor,
                               const Lines& lines,
                               bool isInner,
                               char quote,
                               int leftColOffset = 0,
                               int rightColOffset = 0);

CharRange bracketTextObjectRange(CursorPos cursor,
                                 const Lines& lines,
                                 bool isInner,
                                 char open,
                                 char close,
                                 int leftColOffset = 0,
                                 int rightColOffset = 0);

// =============================================================================
// Paragraph Endpoint/CharRange Computation
// =============================================================================

// Returns the line number where a paragraph motion lands.
//
// Boundary checking via hasLinesOutside:
//   forward:  returns LINE_OUTSIDE_BOUNDARY if endpoint >= lastLine
//             (pass hasLinesBelow)
//   backward: returns LINE_OUTSIDE_BOUNDARY if endpoint <= 0
//             (pass hasLinesAbove)
//
// LineEdgeType is DIRECTION-INDEPENDENT:
//   BlockEdge: edge of current same-type block (blank or non-blank)
//   GapEdge:   edge of blank line run (adjacent to paragraph)
//   NextEdge:  start/end of next different-type block

// Templated version for compile-time dispatch on Forward and Edge
template<bool Forward, LineEdgeType Edge>
int motionParagraphEndpoint(int cursorLine,
                            const Lines& lines,
                            bool hasLinesOutside = false);

// Runtime dispatch version (for internal use in text object functions)
int motionParagraphEndpoint(int cursorLine,
                            const Lines& lines,
                            bool forward,
                            LineEdgeType edgeType);

// Returns the line range for a paragraph text object.
// If boundaries >= 0 and result would cross:
//   returns LINE_RANGE_OUTSIDE_BOUNDARY if range.beginLine <= topBoundary or range.endLine > bottomBoundary
//
// From boundary-logic.md:
//   dip: (Backward, BlockEdge) + (Forward, BlockEdge)
//   dap: depends on cursor position and trailing blank lines
LineRange paragraphTextObjectRange(int cursorLine,
                                   const Lines& lines,
                                   bool isInner,
                                   int topBoundary = -1,
                                   int bottomBoundary = -1);

// =============================================================================
// Sentence Endpoint/CharRange Computation
// =============================================================================

// Returns the position where a pure sentence motion lands, with optional
// boundary checks for sliced buffers.
CursorPos sentenceMotionEndpoint(CursorPos cursor,
                                 const Lines& lines,
                                 bool forward,
                                 int boundaryOffset = 0,
                                 bool hasLinesOutside = false);

// Returns the exclusive endpoint used by sentence operators (`d)`, `c)`,
// `d(`, `c(`), with optional boundary checks for sliced buffers.
CursorPos sentenceOperatorEndpoint(CursorPos cursor,
                                   const Lines& lines,
                                   bool forward,
                                   int boundaryOffset = 0,
                                   bool hasLinesOutside = false);

// Returns the range for a sentence text object.
// If boundaries are valid and result would cross:
//   returns CHAR_RANGE_OUTSIDE_BOUNDARY if range.begin <= leftBoundary or range.end > rightBoundary
//
// From boundary-logic.md:
//   dis: sentence content
//   das: sentence content plus trailing separator, or leading separator if needed
CharRange sentenceTextObjectRange(CursorPos cursor,
                              const Lines& lines,
                              bool isInner,
                              CursorPos leftBoundary = POSITION_OUTSIDE_BOUNDARY,
                              CursorPos rightBoundary = POSITION_OUTSIDE_BOUNDARY);

// =============================================================================
// Scroll Endpoint Computation
// =============================================================================

// Sentinel for line outside boundary
constexpr int LINE_OUTSIDE_BOUNDARY = -1;

// Returns the line endpoint for scroll motions (<C-d>, <C-u>, <C-f>, <C-b>).
// Computes targetLine = cursorLine +/- shift, clamped to buffer bounds.
//
// Boundary check:
//   down (shift > 0): returns LINE_OUTSIDE_BOUNDARY if hasLinesBelow and
//                     targetLine >= lastLine (motion may have been clamped)
//   up (shift < 0):   returns LINE_OUTSIDE_BOUNDARY if hasLinesAbove and
//                     targetLine <= 0 (motion may have been clamped)
int scrollEndpoint(int cursorLine,
                   int numLines,
                   int shift,
                   bool hasLinesAbove,
                   bool hasLinesBelow);

// =============================================================================
// Line Endpoint/CharRange Computation
// =============================================================================

// Sentinel for column outside boundary
constexpr int COL_OUTSIDE_BOUNDARY = -1;

// Returns the column endpoint for line-based motions.
// forward=true:  D/d$ - returns last column of current line
// forward=false: d0   - returns column 0
//
// Boundary check:
//   forward:  if on last line and !boundary.atLineEnd(), returns COL_OUTSIDE_BOUNDARY
//   backward: if on first line and !boundary.atLineStart(), returns COL_OUTSIDE_BOUNDARY
int motionLineEndpoint(CursorPos cursor,
                       const Lines& lines,
                       bool forward,
                       const TransformBoundary& boundary);

// Returns the line range for a full-line delete (dd).
// Returns LINE_RANGE_OUTSIDE_BOUNDARY if would cross boundary.
//
// Safe if:
//   - cursor is on a middle line (not first or last), OR
//   - cursor is on first line AND boundary.atLineStart(), OR
//   - cursor is on last line AND boundary.atLineEnd()
LineRange lineDeleteRange(CursorPos cursor,
                          const Lines& lines,
                          const TransformBoundary& boundary);

} // namespace VimCore
