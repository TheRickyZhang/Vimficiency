#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "Boundary/TransformBoundary.h"
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
  // Character-precise edit region bounds in this diff's buffer context.
  // Half-open: [beginPos, endPos) defines exactly which characters need to change
  CursorPos beginPos;  // First character that differs (inclusive)
  CursorPos endPos;    // One past last character (exclusive, half-open)

  // The actual content being deleted/inserted (flattened with \n for newlines)
  std::string deletedText;   // Characters being removed (may contain \n)
  std::string insertedText;  // Characters being added (may contain \n)

  // Pre-computed boundary info for TransformOptimizer
  // Computed once per DiffState, used for per-position safety checking
  TransformBoundary boundary;

  // Constructor with all fields
  DiffState(CursorPos begin, CursorPos end, std::string deleted,
            std::string inserted, TransformBoundary bnd)
      : beginPos(begin), endPos(end), deletedText(std::move(deleted)),
        insertedText(std::move(inserted)), boundary(std::move(bnd)) {}

  // Convert to Lines format for TransformOptimizer compatibility
  Lines deletedLines() const { return Lines::unflatten(deletedText); }
  Lines insertedLines() const { return Lines::unflatten(insertedText); }

  // Derived accessors
  int origLineStart() const { return beginPos.line; }
  int origLineCount() const { return deletedLines().size(); }
  int newLineStart() const { return beginPos.line; }
  int newLineCount() const { return insertedLines().size(); }

  int origCharCount() const { return static_cast<int>(deletedText.size()); }
  int newCharCount() const { return static_cast<int>(insertedText.size()); }

  // Convenience: is this a pure insertion, deletion, or replacement?
  // With half-open semantics: empty range (beginPos == endPos) means no deleted content
  bool isPureInsertion() const { return beginPos == endPos && !insertedText.empty(); }
  bool isPureDeletion() const { return beginPos != endPos && insertedText.empty(); }
  bool isReplacement() const { return beginPos != endPos && !insertedText.empty(); }
  bool hasDeletedContent() const { return beginPos != endPos; }

  bool contains(const CursorPos& pos) const {
    return pos >= beginPos && pos < endPos;
  }

  int editEndLine() const {
    return endPos.line + (endPos.col > 0 ? 1 : 0);
  }

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

namespace DiffText {

int positionToFlatIndex(const CursorPos& pos, const Lines& lines);
CursorPos flatIndexToPosition(int idx, std::string_view flatText);
CursorPos flatIndexToPosition(int idx, const Lines& lines);
CursorPos advancePositionByText(CursorPos pos, std::string_view text);

std::optional<DiffState> calculateContiguousResidualDiff(
    const Lines& from,
    const Lines& to);

} // namespace DiffText

class OriginalDiffMapper {
public:
  int mapFlatIndex(int originalFlat) const;

  DiffState mapDiffToCurrent(
      const DiffState& originalDiff,
      const Lines& originalLines,
      const Lines& currentLines) const;

  void recordApplied(
      const DiffState& originalDiff,
      const Lines& originalLines);

private:
  struct AppliedSpan {
    int begin = 0;
    int end = 0;
    int delta = 0;
  };
  std::vector<AppliedSpan> applied_;
};
