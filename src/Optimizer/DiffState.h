#pragma once

#include <vector>

#include "EditBoundary.h"
#include "Editor/Position.h"
#include "Utils/Lines.h"

// Represents a single contiguous change region at CHARACTER granularity.
// Unlike line-level diffs, this can represent:
// - Partial line changes (e.g., "aaa bbb" -> "aaa ccc" = just "bbb"->"ccc")
// - Changes spanning multiple lines
// - Changes starting/ending mid-line
struct DiffState {
  // Character-precise edit region bounds (in original buffer coordinates)
  // These define exactly which characters need to change
  Position posBegin;  // First character that differs (inclusive)
  Position posEnd;    // Last character that differs (inclusive)

  // The actual content being deleted/inserted (flattened with \n for newlines)
  std::string deletedText;   // Characters being removed (may contain \n)
  std::string insertedText;  // Characters being added (may contain \n)

  // Pre-computed boundary info for EditOptimizer
  // Computed once per DiffState, used for per-position safety checking
  EditBoundary boundary;

  // Convert to Lines format for EditOptimizer compatibility
  Lines deletedLines() const { return Lines::unflatten(deletedText); }
  Lines insertedLines() const { return Lines::unflatten(insertedText); }

  // Derived accessors
  int origLineStart() const { return posBegin.line; }
  int origLineCount() const { return deletedLines().size(); }
  int newLineStart() const { return posBegin.line; }  // Same as origLineStart after adjustment
  int newLineCount() const { return insertedLines().size(); }

  int origCharCount() const { return static_cast<int>(deletedText.size()); }
  int newCharCount() const { return static_cast<int>(insertedText.size()); }

  // Convenience: is this a pure insertion, deletion, or replacement?
  bool isPureInsertion() const { return deletedText.empty() && !insertedText.empty(); }
  bool isPureDeletion() const { return !deletedText.empty() && insertedText.empty(); }
  bool isReplacement() const { return !deletedText.empty() && !insertedText.empty(); }
};

// Character-level Myers diff algorithm.
// Finds minimal edit script between two buffers at character granularity.
// Returns a sequence of DiffStates representing contiguous change regions.
namespace Myers {

// Calculate minimal diff between startLines and endLines at character level.
// Returns vector of DiffStates in document order, each representing
// a contiguous region where characters differ.
std::vector<DiffState> calculate(
    const Lines& startLines,
    const Lines& endLines);

// Apply a DiffState to transform lines.
// Returns new lines with the diff applied.
Lines applyDiffState(
    const DiffState& diff,
    const Lines& lines);

// Adjust diff indices for sequential application.
// Input diffs have indices relative to original buffer.
// Output diffs have indices relative to buffer state after all previous diffs.
// Use this when applying diffs one at a time to track intermediate states.
std::vector<DiffState> adjustForSequential(
    const std::vector<DiffState>& diffs);

// Apply all diffs in sequence to startLines
Lines applyAllDiffState(
    const std::vector<DiffState>& diffs,
    const Lines& startLines);

} // namespace Myers
