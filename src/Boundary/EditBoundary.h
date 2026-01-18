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
  // Full content before/after the edit region on the same line.
  // - prefix: characters before edit region on first line (empty if at line start)
  // - suffix: characters after edit region on last line (empty if at line end)
  // These enable correct cursor behavior after multi-line deletions merge lines.
  std::string prefix;
  std::string suffix;

  // Whether there are lines above/below the edit region in the buffer.
  // Used for vertical boundary detection (gg, G, k, j escaping).
  bool hasLinesAbove = false;
  bool hasLinesBelow = false;

  // Quote/bracket context for text object operations
  QuoteFlags firstLineQuotes;
  QuoteFlags lastLineQuotes;
  BracketFlags firstLineBrackets;
  BracketFlags lastLineBrackets;

  // Default constructor: empty prefix/suffix, no lines above/below
  EditBoundary() = default;

  // Construct from buffer context
  EditBoundary(const Lines& lines, Position startPos, Position endPos);

  // Construct inheriting from parent boundary (for sub-regions)
  EditBoundary(const EditBoundary& parent, const Lines& lines, Position startPos, Position endPos);

  // Helper accessors for boundary char (last char of prefix, first char of suffix)
  char leftChar() const { return prefix.empty() ? (hasLinesAbove ? '\n' : NO_CHAR) : prefix.back(); }
  char rightChar() const { return suffix.empty() ? (hasLinesBelow ? '\n' : NO_CHAR) : suffix.front(); }

  bool atLineEnd() const { return suffix.empty(); }
  bool atLineStart() const { return prefix.empty(); }

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
