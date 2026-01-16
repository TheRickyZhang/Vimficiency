#pragma once

#include "EdgeType.h"
#include "LineEdgeType.h"
#include "SentenceEdgeType.h"
#include "Editor/Range.h"
#include "Editor/LineRange.h"
#include "Utils/Lines.h"

// =============================================================================
// VimEndpointUtils - Endpoint and Range computation for boundary checking
// =============================================================================
//
// These functions return positions/ranges without modifying state.
// They are used during A* search to predict whether motions cross boundaries.
//
// See boundary-logic.md for the crossing table model.
//
// Parallel to VimMovementUtils (which has void-returning motion functions),
// this struct provides the endpoint/range variants.

struct VimEndpointUtils {
  // ==========================================================================
  // Word endpoint/range computation
  // ==========================================================================

  // Returns the endpoint position of a word motion.
  // If boundary is valid and result would cross it:
  //   forward:  returns POSITION_OUTSIDE_BOUNDARY if endpoint >= boundary
  //   backward: returns POSITION_OUTSIDE_BOUNDARY if endpoint <= boundary
  static Position motionWordEndpoint(Position cursor,
                                     const Lines &lines,
                                     bool forward,
                                     EdgeType edgeType,
                                     bool big,
                                     bool skipCurrent = false,
                                     Position boundary = POSITION_OUTSIDE_BOUNDARY);

  // Returns the range that a word text object would select.
  // If boundaries are valid and result would cross:
  //   returns RANGE_OUTSIDE_BOUNDARY if range.start <= leftBoundary or range.end >= rightBoundary
  //
  // From boundary-logic.md:
  //   diw/diW: (Backward, WordEdge) + (Forward, WordEdge)
  //   daw/daW: depends on cursor position and trailing whitespace
  static Range textObjectRange(
      Position cursor,
      const Lines& lines,
      bool isInner,      // true for iw/iW, false for aw/aW
      bool isBigWord,    // true for W variants
      Position leftBoundary = POSITION_OUTSIDE_BOUNDARY,
      Position rightBoundary = POSITION_OUTSIDE_BOUNDARY);

  // ==========================================================================
  // Paragraph endpoint/range computation
  // ==========================================================================

  // Returns the line number where a paragraph motion lands.
  // If boundaryLine >= 0 and result would cross it:
  //   forward:  returns -1 if endpointLine >= boundaryLine
  //   backward: returns -1 if endpointLine <= boundaryLine
  //
  // LineEdgeType is DIRECTION-INDEPENDENT:
  //   BlockEdge: edge of current same-type block (blank or non-blank)
  //   GapEdge:   edge of blank line run (adjacent to paragraph)
  //   NextEdge:  start/end of next different-type block
  static int motionParagraphEdge(int cursorLine,
                                 const Lines& lines,
                                 bool forward,
                                 LineEdgeType edgeType,
                                 int boundaryLine = -1);

  // Returns the line range for a paragraph text object.
  // If boundaries >= 0 and result would cross:
  //   returns LINE_RANGE_OUTSIDE_BOUNDARY if range.startLine <= topBoundary or range.endLine >= bottomBoundary
  //
  // From boundary-logic.md:
  //   dip: (Backward, BlockEdge) + (Forward, BlockEdge)
  //   dap: depends on cursor position and trailing blank lines
  static LineRange paragraphTextObjectRange(int cursorLine,
                                            const Lines& lines,
                                            bool isInner,
                                            int topBoundary = -1,
                                            int bottomBoundary = -1);

  // ==========================================================================
  // Sentence endpoint/range computation
  // ==========================================================================

  // Returns the position where a sentence motion lands.
  // If boundary is valid and result would cross it:
  //   forward:  returns POSITION_OUTSIDE_BOUNDARY if endpoint >= boundary
  //   backward: returns POSITION_OUTSIDE_BOUNDARY if endpoint <= boundary
  //
  // SentenceEdgeType is DIRECTION-INDEPENDENT:
  //   SentenceEdge: edge of current sentence (punctuation + closers)
  //   GapEdge:      edge of whitespace gap after sentence end
  //   NextEdge:     start of next sentence
  static Position motionSentenceEdge(Position cursor,
                                     const Lines& lines,
                                     bool forward,
                                     SentenceEdgeType edgeType,
                                     Position boundary = POSITION_OUTSIDE_BOUNDARY);

  // Returns the range for a sentence text object.
  // If boundaries are valid and result would cross:
  //   returns RANGE_OUTSIDE_BOUNDARY if range.start <= leftBoundary or range.end >= rightBoundary
  //
  // From boundary-logic.md:
  //   dis: (Backward, SentenceEdge) + (Forward, SentenceEdge)
  //   das: depends on cursor position and trailing whitespace
  static Range sentenceTextObjectRange(Position cursor,
                                       const Lines& lines,
                                       bool isInner,
                                       Position leftBoundary = POSITION_OUTSIDE_BOUNDARY,
                                       Position rightBoundary = POSITION_OUTSIDE_BOUNDARY);
};
