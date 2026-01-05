#include "DiffState.h"

#include <algorithm>
#include <cassert>

using namespace std;

namespace Myers {

// Internal: represents an edit operation
enum class EditOp { KEEP, DELETE, INSERT };

// Find first position where two strings differ
static int findFirstDiff(const string& a, const string& b) {
  int minLen = min(static_cast<int>(a.size()), static_cast<int>(b.size()));
  for (int i = 0; i < minLen; i++) {
    if (a[i] != b[i]) return i;
  }
  return minLen;  // Differ at length boundary
}

// Find last position where two strings differ (returns col in 'a')
static int findLastDiff(const string& a, const string& b) {
  int i = static_cast<int>(a.size()) - 1;
  int j = static_cast<int>(b.size()) - 1;
  while (i >= 0 && j >= 0 && a[i] == b[j]) {
    i--; j--;
  }
  return max(i, 0);
}

// Compute character-precise posBegin and posEnd for a DiffState
static void computeCharBounds(DiffState& diff) {
  if (diff.origLineCount == 0) {
    // Pure insertion: posBegin = posEnd = insertion point
    diff.posBegin = Position(diff.origLineStart, 0);
    diff.posEnd = Position(diff.origLineStart, 0);
    return;
  }

  if (diff.newLineCount == 0) {
    // Pure deletion: entire deleted range
    diff.posBegin = Position(diff.origLineStart, 0);
    int lastLine = diff.origLineStart + diff.origLineCount - 1;
    int lastCol = static_cast<int>(diff.deletedLines.back().size());
    diff.posEnd = Position(lastLine, lastCol);
    return;
  }

  // Replacement: find where content actually differs
  const string& firstDel = diff.deletedLines.front();
  const string& firstIns = diff.insertedLines.front();
  int beginCol = findFirstDiff(firstDel, firstIns);

  const string& lastDel = diff.deletedLines.back();
  const string& lastIns = diff.insertedLines.back();
  int endCol = findLastDiff(lastDel, lastIns);

  // Handle single-line case: ensure begin <= end
  if (diff.origLineCount == 1 && beginCol > endCol) {
    endCol = beginCol;
  }

  diff.posBegin = Position(diff.origLineStart, beginCol);
  diff.posEnd = Position(diff.origLineStart + diff.origLineCount - 1, endCol);
}

// Internal: trace the edit path from the DP table
vector<EditOp> tracePath(const vector<string>& a, const vector<string>& b) {
  int n = static_cast<int>(a.size());
  int m = static_cast<int>(b.size());

  // DP: lcs[i][j] = length of LCS of a[0..i-1] and b[0..j-1]
  vector<vector<int>> lcs(n + 1, vector<int>(m + 1, 0));

  for (int i = 1; i <= n; i++) {
    for (int j = 1; j <= m; j++) {
      if (a[i - 1] == b[j - 1]) {
        lcs[i][j] = lcs[i - 1][j - 1] + 1;
      } else {
        lcs[i][j] = max(lcs[i - 1][j], lcs[i][j - 1]);
      }
    }
  }

  // Trace back to get edit operations
  vector<EditOp> ops;
  int i = n, j = m;
  while (i > 0 || j > 0) {
    if (i > 0 && j > 0 && a[i - 1] == b[j - 1]) {
      ops.push_back(EditOp::KEEP);
      i--; j--;
    } else if (j > 0 && (i == 0 || lcs[i][j - 1] >= lcs[i - 1][j])) {
      ops.push_back(EditOp::INSERT);
      j--;
    } else {
      ops.push_back(EditOp::DELETE);
      i--;
    }
  }

  reverse(ops.begin(), ops.end());
  return ops;
}

// Group consecutive edit operations into DiffStates
vector<DiffState> calculate(const Lines& startLines,
                            const Lines& endLines) {
  vector<EditOp> ops = tracePath(startLines, endLines);

  vector<DiffState> result;
  int origIdx = 0;  // Current position in startLines
  int newIdx = 0;   // Current position in endLines
  size_t opIdx = 0;

  while (opIdx < ops.size()) {
    // Skip KEEP operations
    while (opIdx < ops.size() && ops[opIdx] == EditOp::KEEP) {
      origIdx++;
      newIdx++;
      opIdx++;
    }

    if (opIdx >= ops.size()) break;

    // Found a change region - collect consecutive DELETEs and INSERTs
    DiffState diff;
    diff.origLineStart = origIdx;
    diff.newLineStart = newIdx;
    diff.origLineCount = 0;
    diff.newLineCount = 0;

    // Collect DELETEs first (lines removed from original)
    while (opIdx < ops.size() && ops[opIdx] == EditOp::DELETE) {
      diff.deletedLines.push_back(startLines[origIdx]);
      diff.origLineCount++;
      origIdx++;
      opIdx++;
    }

    // Collect INSERTs (lines added to new)
    while (opIdx < ops.size() && ops[opIdx] == EditOp::INSERT) {
      diff.insertedLines.push_back(endLines[newIdx]);
      diff.newLineCount++;
      newIdx++;
      opIdx++;
    }

    // Compute character-precise bounds
    computeCharBounds(diff);

    result.push_back(std::move(diff));
  }

  return result;
}

Lines applyDiffState(const DiffState& diff, const Lines& lines) {
  Lines result;

  // Copy lines before the change
  for (int i = 0; i < diff.origLineStart; i++) {
    result.push_back(lines[i]);
  }

  // Insert new lines
  for (const auto& line : diff.insertedLines) {
    result.push_back(line);
  }

  // Copy lines after the change (skip deleted lines)
  for (int i = diff.origLineStart + diff.origLineCount;
       i < static_cast<int>(lines.size()); i++) {
    result.push_back(lines[i]);
  }

  return result;
}

// Adjust diff indices for sequential application.
// Input diffs have indices relative to original buffer.
// Output diffs have indices relative to the buffer state after all previous diffs.
vector<DiffState> adjustForSequential(const vector<DiffState>& diffs) {
  vector<DiffState> result;
  result.reserve(diffs.size());

  int lineOffset = 0;  // Cumulative change in line count

  for (const auto& diff : diffs) {
    DiffState adjusted = diff;
    adjusted.origLineStart += lineOffset;
    // newLineStart is already relative to final buffer, but for intermediate
    // states we need to track where in the current buffer we're inserting
    adjusted.newLineStart = adjusted.origLineStart;

    // Adjust posBegin and posEnd line numbers
    adjusted.posBegin.line += lineOffset;
    adjusted.posEnd.line += lineOffset;

    result.push_back(std::move(adjusted));

    // Update offset for next diff
    lineOffset += (diff.newLineCount - diff.origLineCount);
  }

  return result;
}

Lines applyAllDiffState(const vector<DiffState>& diffs,
                        const Lines& startLines) {
  Lines current = startLines;

  // Use adjusted diffs for sequential application
  vector<DiffState> adjusted = adjustForSequential(diffs);

  for (const auto& diff : adjusted) {
    current = applyDiffState(diff, current);
  }

  return current;
}

} // namespace Myers
