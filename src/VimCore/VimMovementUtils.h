#include <vector>
#include <string>

#include "EdgeType.h"
#include "LineEdgeType.h"
#include "Editor/Range.h"
#include "Editor/LineRange.h"
#include "Utils/Lines.h"

struct VimMovementUtils {
  // Fundamental helpers for working with position
  static int clampCol(const Lines &lines,
                      int col,
                      int lineIdx);
  static void moveCol(Position &pos,
                      const Lines &lines,
                      int dx);
  static void moveLine(Position &pos,
                       const Lines &lines,
                       int dy);

  // ==========================================================================
  // Word motions - general interface
  // ==========================================================================
  //
  // Unified word motion based on EdgeType, direction, and word type.
  // This is the fundamental building block; named motions forward to this.
  //
  // EdgeType is DIRECTION-INDEPENDENT:
  //   WordEdge: edge of the word we traverse (step back into word)
  //   GapEdge:  edge of the gap before next word (step back into gap)
  //   NextEdge: edge of the next unit (stay at first char of next thing)
  //
  // Mapping:
  //   Forward  + NextEdge -> w/W  (to start of next word)
  //   Forward  + WordEdge -> e/E  (to end of word)
  //   Backward + WordEdge -> b/B  (to start of word)
  //   Backward + NextEdge -> ge/gE (to end of previous word)
  //
  static void motionWord(Position &pos,
                         const Lines &lines,
                         bool forward,
                         EdgeType edgeType,
                         bool big,
                         bool skipCurrent = false);

  // Returns the endpoint position of a word motion.
  // Caller can compare to boundary: `endpoint >= boundary` (forward) or `endpoint <= boundary` (backward)
  static Position motionWordEndpoint(Position cursor,
                                     const Lines &lines,
                                     bool forward,
                                     EdgeType edgeType,
                                     bool big,
                                     bool skipCurrent = false);

  // ==========================================================================
  // Text object range computation
  // ==========================================================================
  //
  // Text objects (iw, aw, iW, aW) select ranges in both directions from cursor.
  // Returns the range that would be selected.
  //
  // Caller can compare to boundaries:
  //   range.start <= leftBoundary  -> reaches left
  //   range.end >= rightBoundary   -> reaches right
  //
  // From boundary-logic.md:
  //   diw/diW: (Backward, WordEdge) + (Forward, WordEdge)
  //   daw/daW: depends on cursor position and trailing whitespace

  // Returns the range that the text object would select.
  static Range textObjectRange(
      Position cursor,
      const Lines& lines,
      bool isInner,      // true for iw/iW, false for aw/aW
      bool isBigWord);   // true for W variants

  // Named word motion forwarders
  static void motionW(Position &pos,
                      const Lines &lines,
                      bool big);

  static void motionB(Position &pos,
                      const Lines &lines,
                      bool big);

  static void motionE(Position &pos,
                      const Lines &lines,
                      bool big);

  static void motionGe(Position &pos,
                       const Lines &lines,
                       bool big);

  // ==========================================================================
  // Paragraph motions - general interface (parallel to word motions)
  // ==========================================================================
  //
  // Unified paragraph motion based on LineEdgeType and direction.
  // This is the fundamental building block; named motions forward to this.
  //
  // LineEdgeType is DIRECTION-INDEPENDENT (parallel to EdgeType):
  //   BlockEdge: edge of current same-type block (blank or non-blank)
  //   GapEdge:   edge of blank line run (adjacent to paragraph)
  //   NextEdge:  start/end of next different-type block
  //
  // Mapping:
  //   Forward  + NextEdge -> } (to first blank line after paragraph)
  //   Backward + NextEdge -> { (to first blank line before paragraph)
  //
  // Returns the line number where the motion lands.
  static int motionParagraphEdge(int cursorLine,
                                 const Lines& lines,
                                 bool forward,
                                 LineEdgeType edgeType);

  // Returns the line range for a paragraph text object.
  // Caller can compare to boundaries:
  //   range.startLine <= topBoundary  -> reaches top
  //   range.endLine >= bottomBoundary -> reaches bottom
  //
  // From boundary-logic.md:
  //   dip: (Backward, BlockEdge) + (Forward, BlockEdge)
  //   dap: depends on cursor position and trailing blank lines
  static LineRange paragraphTextObjectRange(int cursorLine,
                                            const Lines& lines,
                                            bool isInner);

  // Named paragraph motion forwarders
  static void motionParagraphPrev(Position& pos, const Lines& lines);
  static void motionParagraphNext(Position& pos, const Lines& lines);

  // Helpers for paragraph edges (used internally and by text objects)
  static void moveToParagraphStart(Position& pos, const Lines& lines);
  static void moveToParagraphEnd(Position& pos, const Lines& lines);

  // Sentence motions
  static void motionSentencePrev(Position& pos, const Lines& lines);
  static void motionSentenceNext(Position& pos, const Lines& lines);

  // Character find motions (f/F/t/T)
  // Returns destination column, or -1 if target not found
  // forward: true for f/t, false for F/T
  // till: true for t/T (stop one short), false for f/F (land on target)
  static int findCharInLine(char target, const std::string& line, int startCol, bool forward, bool till);

  template<bool Forward>
  static std::vector<std::tuple<char, int, int>> generateFMotions(int currCol, int targetCol, const std::string& line, int threshold);

};
