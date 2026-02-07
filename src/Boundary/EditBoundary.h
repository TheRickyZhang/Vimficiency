#pragma once

#include "Boundary/BoundaryContext.h"
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
// Related: MotionBoundary stores only offsets (no strings) for lighter-weight
// motion constraint checking. Use context() to convert EditBoundary to
// BoundaryContext for interop with MotionBoundary.
// =============================================================================

struct EditBoundary {
  // Default constructor: empty prefix/suffix, no lines above/below
  EditBoundary() = default;
  static const EditBoundary& noParent();

  // Construct from buffer context, optionally inheriting from parent
  // endPos is exclusive: one past the last valid cursor position on the last line
  EditBoundary(const Lines& lines, Position firstPos, Position endPos,
               const EditBoundary& parent = noParent());

  // Getters for boundary content
  const std::string& prefix() const { return prefix_; }
  const std::string& suffix() const { return suffix_; }
  bool hasLinesAbove() const { return hasLinesAbove_; }
  bool hasLinesBelow() const { return hasLinesBelow_; }

  // Column offsets (derived from prefix/suffix lengths)
  int leftColOffset() const { return static_cast<int>(prefix_.size()); }
  int rightColOffset() const { return static_cast<int>(suffix_.size()); }

  // Convert to BoundaryContext for interop with MotionBoundary
  BoundaryContext context() const {
    BoundaryContext ctx;
    ctx.hasLinesAbove = hasLinesAbove_;
    ctx.hasLinesBelow = hasLinesBelow_;
    ctx.leftColOffset = leftColOffset();
    ctx.rightColOffset = rightColOffset();
    return ctx;
  }

  // Quote/bracket context for text object operations (read-only)
  const QuoteFlags& firstLineQuotes() const { return firstLineQuotes_; }
  const QuoteFlags& lastLineQuotes() const { return lastLineQuotes_; }
  const BracketFlags& firstLineBrackets() const { return firstLineBrackets_; }
  const BracketFlags& lastLineBrackets() const { return lastLineBrackets_; }

  // Helper accessors for boundary char (last char of prefix, first char of suffix)
  char leftChar() const { return prefix_.empty() ? (hasLinesAbove_ ? '\n' : NO_CHAR) : prefix_.back(); }
  char rightChar() const { return suffix_.empty() ? (hasLinesBelow_ ? '\n' : NO_CHAR) : suffix_.front(); }

  bool hasPrefix() const { return !prefix_.empty(); }
  bool hasSuffix() const { return !suffix_.empty(); }

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

  void computeBoundaryChars(const Lines& lines, Position firstPos, Position endPos);
  void scanQuotesAndBrackets(const Lines& lines, Position firstPos, Position endPos);
};
