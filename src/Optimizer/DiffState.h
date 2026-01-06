#pragma once

#include <vector>

#include "EditBoundary.h"
#include "Editor/Position.h"
#include "Utils/Lines.h"

// Represents a single contiguous change region (a "hunk" in git diff terms).
struct DiffState {
  // Character-precise edit region bounds (in original buffer coordinates)
  // These define exactly which characters need to change
  Position posBegin;  // First character that differs
  Position posEnd;    // Last character that differs (inclusive)

  // The actual content being deleted/inserted
  Lines deletedLines;
  Lines insertedLines;

  // Pre-computed boundary info for EditOptimizer
  // Computed once per DiffState, used for per-position safety checking
  EditBoundary boundary;

  // Derived accessors - these are O(1), no storage overhead
  int origLineStart() const { return posBegin.line; }
  int origLineCount() const { return static_cast<int>(deletedLines.size()); }
  int newLineStart() const { return posBegin.line; }  // Same as origLineStart after adjustment
  int newLineCount() const { return static_cast<int>(insertedLines.size()); }

  int origCharCount() const;

  // Convenience: is this a pure insertion, deletion, or replacement?
  bool isPureInsertion() const { return deletedLines.empty() && !insertedLines.empty(); }
  bool isPureDeletion() const { return !deletedLines.empty() && insertedLines.empty(); }
  bool isReplacement() const { return !deletedLines.empty() && !insertedLines.empty(); }
};

// Myers diff algorithm - finds minimal edit script between two line sequences.
// Returns a sequence of DiffStates representing contiguous change regions.
namespace Myers {

// Calculate minimal diff between startLines and endLines.
// Returns vector of DiffStates in document order.
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
