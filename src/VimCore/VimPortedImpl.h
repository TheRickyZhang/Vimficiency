// VimPortedImpl.h
//
// Implementations ported directly from Neovim's C source with minimal
// adaptation. Keeping them in a dedicated file makes the provenance clear
// and avoids re-invention bugs in hand-written alternatives.
//
// Boundary-aware variants accept (boundaryOffset, hasLinesOutside) and
// return POSITION_OUTSIDE_BOUNDARY as soon as the scan position exceeds
// the boundary — an early-break optimization for the explorer.
//
// Source references (Neovim 0.10, commit range circa 2024):
//   findsent()      — search.c
//   current_word()  — textobject.c
//   inc/incl        — memline.c

#pragma once

#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/Range.h"

namespace VimCore {

// =========================================================================
// Sentence motions — ported from findsent() in search.c
// =========================================================================

// Ported findsent(): find next/previous sentence start.
// Matches Neovim exactly (Pos-based NUL model, same 5-step algorithm).
// Returns the destination CursorPos.
CursorPos findsent(CursorPos cursor, const Lines& lines, bool forward);

// Boundary-aware version for the explorer / interpreter with boundary context.
// boundaryOffset and hasLinesOutside follow the same convention as
// motionWordEndpoint:
//   forward:  OOB if result.line >= lastLine (hasLinesOutside)
//             or result.col >= lineLen - boundaryOffset (on last line)
//   backward: OOB if result.line <= 0 (hasLinesOutside)
//             or result.col < boundaryOffset (on line 0)
// Returns POSITION_OUTSIDE_BOUNDARY on early-break.
CursorPos findsentBounded(CursorPos cursor, const Lines& lines, bool forward,
                          int boundaryOffset, bool hasLinesOutside);

// =========================================================================
// Word text objects — ported from current_word() in textobject.c
// =========================================================================

// Ported current_word(): compute iw/aw/iW/aW text object range.
// Returns half-open [begin, end) range matching Neovim's behavior.
// include=true → aw/aW, include=false → iw/iW.
Range currentWord(CursorPos cursor, const Lines& lines,
                  bool include, bool bigword);

// Boundary-aware version: returns Range with POSITION_OUTSIDE_BOUNDARY
// endpoints when the text object range extends into boundary regions.
Range currentWordBounded(CursorPos cursor, const Lines& lines,
                         bool include, bool bigword,
                         int leftColOffset, int rightColOffset,
                         bool hasLinesAbove, bool hasLinesBelow);

// =========================================================================
// Word deletion (dw/dW) — operator adjustment from ops.c
// =========================================================================
//
// Vim's "don't cross lines" rule: when `w` motion would cross to the next
// line and the current line is non-empty, delete only to end of current
// line. This is NOT part of the w motion — it's operator-specific logic
// in Neovim's do_pending_operator().
//
// Returns half-open [begin, end) deletion range for count=1 dw/dW.
// Empty range (begin == end) means nothing to delete.

Range deleteWordForwardRange(CursorPos cursor, const Lines& lines, bool big);

// Boundary-aware version: returns Range with POSITION_OUTSIDE_BOUNDARY
// endpoints when the deletion range extends into boundary regions.
Range deleteWordForwardRangeBounded(CursorPos cursor, const Lines& lines,
                                    bool big, int rightColOffset,
                                    bool hasLinesBelow);

// =========================================================================
// Backward word deletion (db/dB) — operator adjustment from ops.c
// =========================================================================
//
// db/dB use backward word motion with exclusive-at-cursor semantics.
// The returned half-open range follows Vim's cross-line behavior at col 0:
// - previous non-empty line: delete previous word text (without consuming the
//   current line's first char), letting deleteRange() handle line-removal/cursor
//   placement exactly as Vim does.
// - previous empty line: delete the separator newline to remove that blank line.
//
// Returns half-open [begin, end) deletion range for count=1 db/dB.
// Empty range (begin == end) means nothing to delete.
Range deleteWordBackwardRange(CursorPos cursor, const Lines& lines, bool big);

// Boundary-aware version: marks range.begin as POSITION_OUTSIDE_BOUNDARY
// when deletion would cross protected left/top boundary.
Range deleteWordBackwardRangeBounded(CursorPos cursor, const Lines& lines,
                                     bool big, int leftColOffset,
                                     bool hasLinesAbove);

} // namespace VimCore
