#pragma once

#include "Editor/Position.h"
#include "Utils/BracketFlags.h"
#include "Utils/Lines.h"
#include "Utils/NoChar.h"
#include "Utils/QuoteFlags.h"

// =============================================================================
// EditBoundary: Pre-computed boundary info for constrained edit operations
// =============================================================================
// Stores the characters immediately outside the edit region, plus context
// flags for quote/bracket text objects and line-level operations.
//
// Workflow:
// 1. Compute EditBoundary from original text (once per edit region)
// 2. Pass to VimEndpointUtils which uses raw chars for boundary decisions
//
// =============================================================================

struct EditBoundary {
  // What character is immediately outside, if present.
  // - If there's a char on same line, use it
  // - If at line boundary with another line: '\n'
  // - If at buffer boundary (no line above/below): NO_CHAR
  char leftChar = NO_CHAR;
  char rightChar = NO_CHAR;
  bool hasLinesAbove = false;
  bool hasLinesBelow = false;

  // Quote/bracket context for text object operations
  QuoteFlags firstLineQuotes;
  QuoteFlags lastLineQuotes;
  BracketFlags firstLineBrackets;
  BracketFlags lastLineBrackets;

  // Construct from buffer context
  EditBoundary(const Lines& lines, Position startPos, Position endPos);

  // Construct inheriting from parent boundary (for sub-regions)
  EditBoundary(const EditBoundary& parent, const Lines& lines, Position startPos, Position endPos);

  bool atLineEnd() const { return rightChar == '\n' || rightChar == NO_CHAR; }
  bool atLineStart() const { return leftChar == '\n' || leftChar == NO_CHAR; }

  bool isFullLineEditSafe() const { return atLineStart() && atLineEnd(); }

private:
  void computeBoundaryChars(const Lines& lines, Position startPos, Position endPos);
  void scanQuotesAndBrackets(const Lines& lines, Position startPos, Position endPos);
};

/*
// DEPRECATED: CharType-based crossing checks
// These are no longer used - VimEndpointUtils now handles boundary checking
using raw characters directly.

enum class CharType : uint8_t {
  Keyword,    // alphanumeric + underscore (vim 'iskeyword')
  Whitespace, // space, tab, etc.
  Symbol,     // punctuation and other non-word chars
  Newline     // at line boundary (nothing beyond)
};

CharType getCharType(char c);
CharType getOppositeCharType(CharType charType);

bool canEndCross(CharType c, CharType bc);
bool canSpaceCross(CharType c, CharType bc);
bool canNextCross(CharType c, CharType bc);
bool canLineCross(CharType bc);
bool canEndCrossWORD(CharType c, CharType bc);
bool canSpaceCrossWORD(CharType c, CharType bc);
bool canNextCrossWORD(CharType c, CharType bc);
*/
