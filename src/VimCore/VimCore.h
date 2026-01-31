#pragma once

#include <string>
#include <utility>

#include "Editor/Position.h"
#include "Utils/Lines.h"
#include "EdgeType.h"

// VimCore: Character classification, string helpers, position stepping,
// and core word motion logic. Used by VimMotionUtils, VimEndpointUtils,
// and BufferIndex.
//
// Edit operations (deleteRange, insertText, etc.) are in VimEditUtils.h

namespace VimCore {

// =============================================================================
// 1. Character Classification (no dependencies, used by everything)
// =============================================================================

// Space or tab (not newline) - for within-line whitespace skipping
bool isWhitespace(unsigned char c);

// Space, tab, or newline - for general blank checks
bool isBlank(unsigned char c);

// Alphanumeric or underscore - small word characters
bool isSmallWordChar(unsigned char c);

// Non-whitespace non-null - big WORD characters
bool isBigWordChar(unsigned char c);

// [.!?] - sentence ending punctuation (used by BufferIndex)
bool isSentenceEnd(unsigned char c);

// [)'"'\]] - sentence closers
bool isSentenceCloser(unsigned char c);

// =============================================================================
// 2. String/Line Helpers (depends on char classification)
// =============================================================================

// Check if line is blank (empty or whitespace-only)
bool isBlankLineStr(const std::string& s);

// Return column of first non-blank character (or 0 if all blank)
int firstNonBlankColInLineStr(const std::string& s);

// =============================================================================
// 3. Position Stepping (depends on Lines)
// =============================================================================

// Modern Position-based API (inline)
inline Position step(const Lines& lines, Position pos, bool forward) {
  return forward ? lines.getNextPos(pos) : lines.getPrevPos(pos);
}

inline Position stepBack(const Lines& lines, Position pos, bool forward) {
  return forward ? lines.getPrevPos(pos) : lines.getNextPos(pos);
}

// Old int-based API (for sentence/paragraph helpers)
unsigned char getChar(const Lines& lines, int line, int col);
bool stepFwd(const Lines& lines, int& line, int& col);
bool stepBack(const Lines& lines, int& line, int& col);

// =============================================================================
// 4. Word Motion Core (depends on stepping + char classification)
// =============================================================================

// Core motion that returns raw result - POSITION_OUTSIDE_BOUNDARY if motion
// would go past buffer boundary. Used by VimEndpointUtils for boundary detection.
//
// lineBounded: When true, motion stops at line boundaries instead of crossing.
//   - Backward from whitespace: returns Position(line, 0) instead of crossing to prev line
//   - Forward through whitespace: returns Position(line, lastCol) instead of crossing to next line
//   Used by daw text object which doesn't cross lines from indentation.
Position motionWordCore(Position pos,
                        const Lines& lines,
                        bool forward,
                        EdgeType edgeType,
                        bool big,
                        bool skipCurrent,
                        bool lineBounded = false);

// =============================================================================
// 5. Paragraph Helpers (depends on line classification)
// =============================================================================

// Returns the first line index of the paragraph containing lineIdx.
int paragraphStartLine(const Lines& lines, int lineIdx);

// Returns the last line index of the paragraph containing lineIdx.
int paragraphEndLine(const Lines& lines, int lineIdx);

// =============================================================================
// 6. Sentence Helpers (depends on char classification + stepping)
// =============================================================================

// Check if position is a sentence end: [.!?] + optional closers + (whitespace or EOL)
bool isSentenceEndAt(const Lines& lines, int line, int col);

// From sentence end, skip past closers and whitespace to find next sentence start.
// Returns (line, col) of next sentence start.
std::pair<int, int> skipToSentenceStart(const Lines& lines,
                                        int line, int col);

// Find the start of the sentence containing position (line, col).
// Returns (line, col) of sentence start.
std::pair<int, int> findCurrentSentenceStart(const Lines& lines,
                                              int line, int col);

} // namespace VimCore
