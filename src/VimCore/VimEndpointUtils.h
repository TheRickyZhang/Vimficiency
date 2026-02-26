#pragma once

#include "Types/EdgeType.h"
#include "Types/LineEdgeType.h"
#include "Types/SentenceEdgeType.h"
#include "Types/Range.h"
#include "Types/LineRange.h"
#include "Types/Lines.h"

// Forward declaration
struct EditBoundary;

namespace VimCore {

// =============================================================================
// Endpoint and Range Computation for Boundary Checking
// =============================================================================
//
// These functions return positions/ranges without modifying state.
// They are used during A* search to predict whether motions cross boundaries.
//
// See boundary-logic.md for the crossing table model.

// =============================================================================
// Word Endpoint/Range Computation
// =============================================================================

// Returns the endpoint position of a word motion.
//
// Boundary checking via boundaryOffset:
//   forward:  returns POSITION_OUTSIDE_BOUNDARY if endpoint is in suffix region
//             (last boundaryOffset cols of last line)
//   backward: returns POSITION_OUTSIDE_BOUNDARY if endpoint is in prefix region
//             (first boundaryOffset cols of line 0)
//   boundaryOffset <= 0: no column boundary checking
//
// Boundary checking via hasLinesOutside:
//   forward:  returns POSITION_OUTSIDE_BOUNDARY if endpoint.line > lastLine
//             (pass hasLinesBelow)
//   backward: returns POSITION_OUTSIDE_BOUNDARY if endpoint.line < 0
//             (pass hasLinesAbove)
//
// lineBounded: When true, motion stops at line boundaries instead of crossing.
//   Used by daw text object which doesn't cross lines from indentation.

// Templated version for compile-time dispatch on Forward and Edge
template<bool Forward, EdgeType Edge>
CursorPos motionWordEndpoint(CursorPos cursor,
                            const Lines& lines,
                            bool big,
                            bool skipCurrent,
                            int boundaryOffset,
                            bool hasLinesOutside,
                            bool lineBounded = false);

// Runtime dispatch version (for compatibility)
CursorPos motionWordEndpoint(CursorPos cursor,
                            const Lines& lines,
                            bool forward,
                            EdgeType edgeType,
                            bool big,
                            bool skipCurrent,
                            int boundaryOffset,
                            bool hasLinesOutside,
                            bool lineBounded);

// Convert a word-motion endpoint to a half-open range end.
// For forward motions:
// - WordEdge/GapEdge endpoints are inclusive and are advanced by one char.
// - NextEdge endpoints are already exclusive.
CursorPos wordEndpointToRangeEnd(CursorPos endpoint,
                                         const Lines& lines,
                                         EdgeType edgeType);

// Computes the raw range that a word text object would select.
// Returns Range where start/end could be POSITION_OUTSIDE_BOUNDARY on overflow.
// Used internally - prefer textObject() for execution.
//
// From boundary-logic.md:
//   diw/diW: (Backward, WordEdge) + (Forward, WordEdge)
//   daw/daW: depends on cursor position and trailing whitespace
Range textObjectCore(
    CursorPos cursor,
    const Lines& lines,
    bool isInner,       // true for iw/iW, false for aw/aW
    bool isBigWord);    // true for W variants

// Computes the range that a word text object would select.
// Clamps POSITION_OUTSIDE_BOUNDARY to buffer edges - always returns valid Range.
// Used for executing text objects (no boundary checking).
Range textObject(
    CursorPos cursor,
    const Lines& lines,
    bool isInner,       // true for iw/iW, false for aw/aW
    bool isBigWord);    // true for W variants

// Returns the range that a word text object would select, with boundary checking.
// Returns half-open Range [begin, end), where either endpoint can be
// POSITION_OUTSIDE_BOUNDARY if crossing occurs.
//
// Boundary checking (same model as motionWordEndpoint):
//   leftColOffset > 0:  crosses if range.begin is in prefix region
//                       (first leftColOffset cols of line 0)
//   rightColOffset > 0: crosses if range.end is in suffix region
//                       (last rightColOffset cols of last line)
//   hasLinesAbove:      crosses if backward motion goes past line 0
//   hasLinesBelow:      crosses if forward motion goes past last line
Range textObjectRange(
    CursorPos cursor,
    const Lines& lines,
    bool isInner,        // true for iw/iW, false for aw/aW
    bool isBigWord,      // true for W variants
    int leftColOffset,   // columns protected at start of line 0
    int rightColOffset,  // columns protected at end of last line
    bool hasLinesAbove,  // are there lines above the edit region?
    bool hasLinesBelow); // are there lines below the edit region?

// =============================================================================
// Paragraph Endpoint/Range Computation
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
// Sentence Endpoint/Range Computation
// =============================================================================

// Sentence motions now use VimCore::findsent()/findsentBounded() from
// VimPortedImpl (ported from Neovim). This file keeps only sentence
// text-object edge/range utilities.

// Convert a sentence-motion endpoint to a half-open range end.
// For forward motions:
// - SentenceEdge/GapEdge endpoints are inclusive and are advanced by one char.
// - NextEdge endpoints are already exclusive.
CursorPos sentenceEndpointToRangeEnd(CursorPos endpoint,
                                             const Lines& lines,
                                             SentenceEdgeType edgeType);

// Returns the range for a sentence text object.
// If boundaries are valid and result would cross:
//   returns RANGE_OUTSIDE_BOUNDARY if range.begin <= leftBoundary or range.end > rightBoundary
//
// From boundary-logic.md:
//   dis: (Backward, SentenceEdge) + (Forward, SentenceEdge)
//   das: depends on cursor position and trailing whitespace
Range sentenceTextObjectRange(CursorPos cursor,
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
// Line Endpoint/Range Computation
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
                       const EditBoundary& boundary);

// Returns the line range for a full-line delete (dd).
// Returns LINE_RANGE_OUTSIDE_BOUNDARY if would cross boundary.
//
// Safe if:
//   - cursor is on a middle line (not first or last), OR
//   - cursor is on first line AND boundary.atLineStart(), OR
//   - cursor is on last line AND boundary.atLineEnd()
LineRange lineDeleteRange(CursorPos cursor,
                          const Lines& lines,
                          const EditBoundary& boundary);

} // namespace VimCore
