#pragma once

#include <vector>

#include "EditBoundary.h"
#include "Editor/Position.h"
#include "Utils/Lines.h"

// Represents a single contiguous change region (a "hunk" in git diff terms).
struct DiffState {
  // Position in the ORIGINAL buffer where this change starts
  int origLineStart;  // 0-indexed line in startLines
  int origLineCount;  // Number of lines deleted from original

  // Position in the NEW buffer where inserted content appears
  int newLineStart;   // 0-indexed line in endLines
  int newLineCount;   // Number of lines inserted

  Lines deletedLines;
  Lines insertedLines;

  // Character-precise edit region bounds (in original buffer coordinates)
  // These define exactly which characters need to change
  Position posBegin;  // First character that differs
  Position posEnd;    // Last character that differs (inclusive)

  // Pre-computed boundary info for EditOptimizer
  // Computed once per DiffState, used for per-position safety checking
  EditBoundary boundary;

  // Convenience: is this a pure insertion, deletion, or replacement?
  bool isPureInsertion() const { return origLineCount == 0 && newLineCount > 0; }
  bool isPureDeletion() const { return origLineCount > 0 && newLineCount == 0; }
  bool isReplacement() const { return origLineCount > 0 && newLineCount > 0; }
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
