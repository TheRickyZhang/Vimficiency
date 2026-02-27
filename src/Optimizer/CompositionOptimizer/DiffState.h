#pragma once

#include <ostream>
#include <string_view>
#include <vector>

#include "Boundary/EditBoundary.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"

// Represents a single contiguous change region at CHARACTER granularity.
// Unlike line-level diffs, this can represent:
// - Partial line changes (e.g., "aaa bbb" -> "aaa ccc" = just "bbb"->"ccc")
// - Changes spanning multiple lines
// - Changes starting/ending mid-line
//
// Uses HALF-OPEN interval semantics: [beginPos, endPos)
// - beginPos: First character that differs (inclusive)
// - endPos: One past last character that differs (exclusive, may be virtual column)
// Benefits: Empty ranges are natural (beginPos == endPos), no -1 adjustments needed.
struct DiffState {
  // Character-precise edit region bounds (in original buffer coordinates)
  // Half-open: [beginPos, endPos) defines exactly which characters need to change
  CursorPos beginPos;  // First character that differs (inclusive)
  CursorPos endPos;    // One past last character (exclusive, half-open)

  // The actual content being deleted/inserted (flattened with \n for newlines)
  std::string deletedText;   // Characters being removed (may contain \n)
  std::string insertedText;  // Characters being added (may contain \n)

  // Pre-computed boundary info for EditOptimizer
  // Computed once per DiffState, used for per-position safety checking
  EditBoundary boundary;

  // Constructor with all fields
  DiffState(CursorPos begin, CursorPos end, std::string deleted,
            std::string inserted, EditBoundary bnd)
      : beginPos(begin), endPos(end), deletedText(std::move(deleted)),
        insertedText(std::move(inserted)), boundary(std::move(bnd)) {}

  // Convert to Lines format for EditOptimizer compatibility
  Lines deletedLines() const { return Lines::unflatten(deletedText); }
  Lines insertedLines() const { return Lines::unflatten(insertedText); }

  // Derived accessors
  int origLineStart() const { return beginPos.line; }
  int origLineCount() const { return deletedLines().size(); }
  int newLineStart() const { return beginPos.line; }  // Same as origLineStart after adjustment
  int newLineCount() const { return insertedLines().size(); }

  int origCharCount() const { return static_cast<int>(deletedText.size()); }
  int newCharCount() const { return static_cast<int>(insertedText.size()); }

  // Convenience: is this a pure insertion, deletion, or replacement?
  // With half-open semantics: empty range (beginPos == endPos) means no deleted content
  bool isPureInsertion() const { return beginPos == endPos && !insertedText.empty(); }
  bool isPureDeletion() const { return beginPos != endPos && insertedText.empty(); }
  bool isReplacement() const { return beginPos != endPos && !insertedText.empty(); }
  bool hasDeletedContent() const { return beginPos != endPos; }

  // Does insertedText end with a newline? (for o/A+<CR> insertion strategies)
  bool isNewLineInsertion() const { return !insertedText.empty() && insertedText.back() == '\n'; }

  // Formats as: "ins 'text'", "del 'text'", or "'old'->'new'"
  friend std::ostream& operator<<(std::ostream& os, const DiffState& d);

  // insertedText without trailing \n (when the mode-entry command handles the newline)
  std::string_view insertedTextBody() const {
    if (isNewLineInsertion()) {
      return std::string_view(insertedText.data(), insertedText.size() - 1);
    }
    return insertedText;
  }
};

// Character-level Myers diff algorithm.
// Finds minimal edit script between two buffers at character granularity.
// Returns a sequence of DiffStates representing contiguous change regions.
namespace Myers {

// Calculate minimal diff between initialLines and goalLines at character level.
// Returns vector of DiffStates in document order, each representing
// a contiguous region where characters differ.
std::vector<DiffState> calculate(
    const Lines& initialLines,
    const Lines& goalLines);

// Apply a DiffState to transform lines.
// Returns new lines with the diff applied.
Lines applyDiffState(
    const DiffState& diff,
    const Lines& lines);

// Apply all diffs in sequence to initialLines
Lines applyAllDiffState(
    const std::vector<DiffState>& diffs,
    const Lines& initialLines);

} // namespace Myers
