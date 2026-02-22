#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <tuple>

#include "VimTypes/EdgeType.h"
#include "VimTypes/Lines.h"

namespace VimCore {

// =============================================================================
// Position Helpers
// =============================================================================

// Clamp column to valid range for given line
int clampCol(const Lines& lines, int col, int lineIdx);

// Move column by dx, clamping to valid range
void moveCol(Position& pos, const Lines& lines, int dx);

// Move line by dy, clamping to valid range and updating column
void moveLine(Position& pos, const Lines& lines, int dy);

// =============================================================================
// Word Motions
// =============================================================================
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

// Motion that clamps result to valid positions (standard vim behavior)
void motionWord(Position& pos,
                const Lines& lines,
                bool forward,
                EdgeType edgeType,
                bool big,
                bool skipCurrent = false);

// Named word motion forwarders
void motionW(Position& pos, const Lines& lines, bool big);
void motionB(Position& pos, const Lines& lines, bool big);
void motionE(Position& pos, const Lines& lines, bool big);
void motionGe(Position& pos, const Lines& lines, bool big);

// =============================================================================
// Paragraph Motions
// =============================================================================

void motionParagraphPrev(Position& pos, const Lines& lines);
void motionParagraphNext(Position& pos, const Lines& lines);

// Helpers for paragraph edges (used internally and by text objects)
void moveToParagraphStart(Position& pos, const Lines& lines);
void moveToParagraphEnd(Position& pos, const Lines& lines);

// =============================================================================
// Sentence Motions
// =============================================================================

void motionSentencePrev(Position& pos, const Lines& lines);
void motionSentenceNext(Position& pos, const Lines& lines);

// =============================================================================
// Character Find Motions (f/F/t/T)
// =============================================================================

// Returns destination column, or -1 if target not found
// forward: true for f/t, false for F/T
// till: true for t/T (stop one short), false for f/F (land on target)
int findCharInLine(char target, std::string_view line, int startCol, bool forward, bool till);

template<bool Forward>
std::vector<std::tuple<char, int, int>> generateFMotions(int currCol, int targetCol, std::string_view line, int threshold);

} // namespace VimCore
