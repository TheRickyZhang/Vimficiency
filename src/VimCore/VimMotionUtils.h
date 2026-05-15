#pragma once

#include <vector>
#include <string_view>
#include <tuple>

#include "Types/Lines.h"

namespace VimCore {

// =============================================================================
// CursorPos Helpers
// =============================================================================

// Clamp column to valid range for given line
int clampCol(const Lines& lines, int col, int lineIdx);

// Move column by dx, clamping to valid range
void moveCol(CursorPos& pos, const Lines& lines, int dx);

// Move line by dy, clamping to valid range and updating column
void moveLine(CursorPos& pos, const Lines& lines, int dy);

// =============================================================================
// Word Motions
// =============================================================================

// Named word motion forwarders
void motionW(CursorPos& pos, const Lines& lines, bool big);
void motionB(CursorPos& pos, const Lines& lines, bool big);
void motionE(CursorPos& pos, const Lines& lines, bool big);
void motionGe(CursorPos& pos, const Lines& lines, bool big);

// =============================================================================
// Paragraph Motions
// =============================================================================

void motionParagraphPrev(CursorPos& pos, const Lines& lines);
void motionParagraphNext(CursorPos& pos, const Lines& lines);

// Helpers for paragraph edges (used internally and by text objects)
void moveToParagraphStart(CursorPos& pos, const Lines& lines);
void moveToParagraphEnd(CursorPos& pos, const Lines& lines);

// =============================================================================
// Sentence Motions
// =============================================================================

void motionSentencePrev(CursorPos& pos, const Lines& lines);
void motionSentenceNext(CursorPos& pos, const Lines& lines);

// =============================================================================
// Character Find Motions (f/F/t/T)
// =============================================================================

// Returns destination column, or -1 if target not found
// forward: true for f/t, false for F/T
// till: true for t/T (stop one short), false for f/F (land on target)
int findCharInLine(char target, std::string_view line, int startCol, bool forward, bool till);
int findCharInLine(char target, std::string_view line, int startCol,
                   bool forward, bool till, int count, bool repeat);

template<bool Forward>
std::vector<std::tuple<char, int, int>> generateFMotions(int currCol, int targetCol, std::string_view line, int threshold);

} // namespace VimCore
