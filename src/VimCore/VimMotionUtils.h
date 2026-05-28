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

// Operator-pending sentence motion: runs Neovim's findsent and leaves cursor
// at the raw motion target (potentially col == lines[line].size() to represent
// the NUL/past-EOL position). Returns true on success, false if findsent
// would FAIL (operator should not apply). Used by sentenceOperatorEndpoint.
bool findSentenceForOperator(CursorPos& cursor, const Lines& lines, bool forward, int count);

// Operator-pending word motions: faithful ports of Neovim's fwd_word /
// end_word / bck_word / bckend_word with eol=true. These implement the
// "newline not included on line crossing" rule and the "stop at empty line"
// rule that the non-operator motions don't.
void motionWForOperator(CursorPos& pos, const Lines& lines, bool big);
void motionEForOperator(CursorPos& pos, const Lines& lines, bool big);
void motionBForOperator(CursorPos& pos, const Lines& lines, bool big);
void motionGeForOperator(CursorPos& pos, const Lines& lines, bool big);

// Count-aware operator-pending word motions. Mirror Vim's bck_word /
// bckend_word / fwd_word / end_word with a count; return `false` when the
// motion FAILs (Vim's `do_pending_operator` clearopbeep), `true` when it
// completes successfully even if the cursor stopped at a boundary mid-count.
bool fwdWordOperator(CursorPos& pos, const Lines& lines, bool big, int count);
bool endWordOperator(CursorPos& pos, const Lines& lines, bool big, int count);
bool bckWordOperator(CursorPos& pos, const Lines& lines, bool big, int count);
bool bckEndWordOperator(CursorPos& pos, const Lines& lines, bool big, int count);

// Faithful port of Vim's current_word from textobject.c. Computes the
// `aw`/`iw`/`aW`/`iW` text object.
// `include`=true → around (`aw`/`aW`), false → inner (`iw`/`iW`).
// `big`=true → big-word (`aW`/`iW`).
// On success (ok=true): `start..end` inclusive is the text object span.
// On failure (ok=false): `end` is where Vim's internal scan ended, which
// matches the cursor position Vim leaves on `clearopbeep` (after the caller
// clamps to a valid normal-mode column).
struct CurrentWordResult {
  CursorPos start;
  CursorPos end;
  bool inclusive;
  bool ok;
};
CurrentWordResult currentWord(CursorPos cursor, const Lines& lines,
                              bool include, bool big);

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
