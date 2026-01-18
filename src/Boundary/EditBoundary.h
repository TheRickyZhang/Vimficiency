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
  // Default constructor: empty prefix/suffix, no lines above/below
  EditBoundary() = default;

  // Construct from buffer context
  EditBoundary(const Lines& lines, Position startPos, Position endPos);

  // Construct inheriting from parent boundary (for sub-regions)
  EditBoundary(const EditBoundary& parent, const Lines& lines, Position startPos, Position endPos);

  // Construct with explicit boundary values (for testing boundary crossing logic)
  EditBoundary(std::string prefix, std::string suffix, bool hasLinesAbove, bool hasLinesBelow)
      : prefix_(std::move(prefix)), suffix_(std::move(suffix)),
        hasLinesAbove_(hasLinesAbove), hasLinesBelow_(hasLinesBelow) {}

  // Getters for boundary content
  const std::string& prefix() const { return prefix_; }
  const std::string& suffix() const { return suffix_; }
  bool hasLinesAbove() const { return hasLinesAbove_; }
  bool hasLinesBelow() const { return hasLinesBelow_; }

  // Quote/bracket context for text object operations (read-only)
  const QuoteFlags& firstLineQuotes() const { return firstLineQuotes_; }
  const QuoteFlags& lastLineQuotes() const { return lastLineQuotes_; }
  const BracketFlags& firstLineBrackets() const { return firstLineBrackets_; }
  const BracketFlags& lastLineBrackets() const { return lastLineBrackets_; }

  // Helper accessors for boundary char (last char of prefix, first char of suffix)
  char leftChar() const { return prefix_.empty() ? (hasLinesAbove_ ? '\n' : NO_CHAR) : prefix_.back(); }
  char rightChar() const { return suffix_.empty() ? (hasLinesBelow_ ? '\n' : NO_CHAR) : suffix_.front(); }

  bool atLineEnd() const { return suffix_.empty(); }
  bool atLineStart() const { return prefix_.empty(); }

  bool isFullLineEditSafe() const { return atLineStart() && atLineEnd(); }

private:
  // Full content before/after the edit region on the same line.
  // - prefix_: characters before edit region on first line (empty if at line start)
  // - suffix_: characters after edit region on last line (empty if at line end)
  // These enable correct cursor behavior after multi-line deletions merge lines.
  std::string prefix_;
  std::string suffix_;

  // Whether there are lines above/below the edit region in the buffer.
  // Used for vertical boundary detection (gg, G, k, j escaping).
  bool hasLinesAbove_ = false;
  bool hasLinesBelow_ = false;

  // Quote/bracket context for text object operations
  QuoteFlags firstLineQuotes_;
  QuoteFlags lastLineQuotes_;
  BracketFlags firstLineBrackets_;
  BracketFlags lastLineBrackets_;

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
