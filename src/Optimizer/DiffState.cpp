#include "DiffState.h"

#include <algorithm>
#include <cassert>
#include <numeric>

using namespace std;

namespace Myers {

// Minimum length for a common substring to be preserved as a separate match.
// Shorter matches are merged into adjacent diffs to produce more intuitive results.
// e.g., "world" -> "there" shares 'r' (len 1), but we want 1 diff, not 2.
// But "migration" -> "arbitrations" shares "ration" (len 6), so 2 diffs makes sense.
const int MIN_MATCH_LENGTH = 4;

// Check if a character is a word boundary (whitespace or punctuation).
// Used to preserve short matches that represent complete words/tokens.
// e.g., " b " in "a b c" -> "d b e" should be preserved even though it's only 3 chars.
// Note: underscore (_) is excluded since it's part of identifiers in code.
static bool isWordBoundaryChar(char c) {
  if (isspace(c)) return true;
  switch (c) {
    case '.': case ',': case ';': case ':': case '!': case '?':
    case '(': case ')': case '[': case ']': case '{': case '}':
    case '"': case '\'': case '`': case '<': case '>':
    case '/': case '\\': case '@': case '#': case '$':
    case '%': case '^': case '&': case '*': case '-':
    case '+': case '=': case '|': case '~':
      return true;
    default:
      return false;
  }
}

// =============================================================================
// Position Mapping Utilities
// =============================================================================

// Convert flat index in flattened text to (line, col) Position
// Flattened text uses \n as line separator
static Position flatIndexToPosition(int idx, const string& flatText) {
  int line = 0;
  int col = 0;
  for (int i = 0; i < idx && i < static_cast<int>(flatText.size()); i++) {
    if (flatText[i] == '\n') {
      line++;
      col = 0;
    } else {
      col++;
    }
  }
  return Position(line, col);
}

// Convert (line, col) Position to flat index in flattened text
static int positionToFlatIndex(const Position& pos, const Lines& lines) {
  int idx = 0;
  for (int i = 0; i < pos.line && i < static_cast<int>(lines.size()); i++) {
    idx += static_cast<int>(lines[i].size()) + 1;  // +1 for \n
  }
  idx += pos.col;
  return idx;
}

// =============================================================================
// Character-Level LCS/Diff
// =============================================================================

// Internal: represents an edit operation
enum class EditOp { KEEP, DELETE, INSERT };

// Trace the edit path from the DP table (character-level)
// Uses FORWARD traceback to prefer early matches and produce contiguous diffs
static vector<EditOp> tracePath(const string& a, const string& b) {
  int n = static_cast<int>(a.size());
  int m = static_cast<int>(b.size());

  // For large strings, use space-optimized approach
  // For now, use standard DP (O(nm) space)
  // TODO: Implement Myers O(nd) or Hirschberg O(n) space if needed

  // DP: lcs[i][j] = length of LCS of a[i..] and b[j..] (suffix LCS)
  // Using suffix LCS enables forward traceback with correct tie-breaking
  vector<vector<int>> lcs(n + 2, vector<int>(m + 2, 0));

  for (int i = n - 1; i >= 0; i--) {
    for (int j = m - 1; j >= 0; j--) {
      if (a[i] == b[j]) {
        lcs[i][j] = lcs[i + 1][j + 1] + 1;
      } else {
        lcs[i][j] = max(lcs[i + 1][j], lcs[i][j + 1]);
      }
    }
  }

  // Forward trace to get edit operations
  // This naturally prefers early matches, producing contiguous diffs
  vector<EditOp> ops;
  int i = 0, j = 0;
  while (i < n || j < m) {
    if (i < n && j < m && a[i] == b[j]) {
      // Characters match - always KEEP
      ops.push_back(EditOp::KEEP);
      i++; j++;
    } else if (j >= m || (i < n && lcs[i + 1][j] >= lcs[i][j + 1])) {
      // DELETE from source: prefer this when it leads to equally good or better LCS
      // This consumes the source first, ensuring early matches
      ops.push_back(EditOp::DELETE);
      i++;
    } else {
      // INSERT from target
      ops.push_back(EditOp::INSERT);
      j++;
    }
  }

  return ops;
}

// =============================================================================
// EditBoundary Computation for Character-Level Diffs
// =============================================================================

// Compute EditBoundary for a character-level diff
// This handles both single-line and multi-line diffs
static EditBoundary computeEditBoundary(
    const Lines& origLines,
    const Position& posBegin,
    const Position& posEnd,
    const string& deletedText) {

  EditBoundary boundary;

  // Line boundary flags
  boundary.startsAtLineStart = (posBegin.col == 0);
  bool endsAtLineEnd = false;
  if (posEnd.line < static_cast<int>(origLines.size())) {
    endsAtLineEnd = (posEnd.col >= static_cast<int>(origLines[posEnd.line].size()) - 1);
  }
  boundary.endsAtLineEnd = endsAtLineEnd;

  // For word/WORD boundary analysis, we need to check the characters
  // adjacent to the edit region in the original buffer

  // Check left boundary (character before posBegin)
  if (posBegin.col > 0 && posBegin.line < static_cast<int>(origLines.size())) {
    const string& line = origLines[posBegin.line];
    int leftPos = posBegin.col - 1;
    int rightPos = posBegin.col;
    if (rightPos < static_cast<int>(line.size())) {
      char leftChar = line[leftPos];
      char rightChar = line[rightPos];
      // Check if they're in same word/WORD
      bool leftIsWord = isalnum(leftChar) || leftChar == '_';
      bool rightIsWord = isalnum(rightChar) || rightChar == '_';
      boundary.left_in_word = leftIsWord && rightIsWord;
      boundary.left_in_WORD = !isspace(leftChar) && !isspace(rightChar);
    }
  }

  // Check right boundary (character after posEnd)
  if (posEnd.line < static_cast<int>(origLines.size())) {
    const string& line = origLines[posEnd.line];
    int leftPos = posEnd.col;
    int rightPos = posEnd.col + 1;
    if (leftPos >= 0 && rightPos < static_cast<int>(line.size())) {
      char leftChar = line[leftPos];
      char rightChar = line[rightPos];
      bool leftIsWord = isalnum(leftChar) || leftChar == '_';
      bool rightIsWord = isalnum(rightChar) || rightChar == '_';
      boundary.right_in_word = leftIsWord && rightIsWord;
      boundary.right_in_WORD = !isspace(leftChar) && !isspace(rightChar);
    }
  }

  return boundary;
}

// =============================================================================
// Main API
// =============================================================================

vector<DiffState> calculate(const Lines& startLines, const Lines& endLines) {
  // Flatten both buffers to character strings
  string startText = startLines.flatten();
  string endText = endLines.flatten();

  // Get character-level edit operations
  vector<EditOp> ops = tracePath(startText, endText);

  vector<DiffState> result;
  int origIdx = 0;  // Current position in startText
  int newIdx = 0;   // Current position in endText
  size_t opIdx = 0;

  // Skip leading KEEPs
  while (opIdx < ops.size() && ops[opIdx] == EditOp::KEEP) {
    origIdx++;
    newIdx++;
    opIdx++;
  }

  while (opIdx < ops.size()) {
    // Found a change region - collect changes, potentially including short KEEP runs
    int startOrigIdx = origIdx;
    string deleted;
    string inserted;

    // Loop: collect changes, and short KEEP runs (< MIN_MATCH_LENGTH)
    while (opIdx < ops.size()) {
      // Collect DELETEs
      while (opIdx < ops.size() && ops[opIdx] == EditOp::DELETE) {
        deleted += startText[origIdx];
        origIdx++;
        opIdx++;
      }

      // Collect INSERTs
      while (opIdx < ops.size() && ops[opIdx] == EditOp::INSERT) {
        inserted += endText[newIdx];
        newIdx++;
        opIdx++;
      }

      // Check for KEEPs - count how many consecutive
      int keepCount = 0;
      size_t peekIdx = opIdx;
      int peekOrigIdx = origIdx;
      int peekNewIdx = newIdx;
      while (peekIdx < ops.size() && ops[peekIdx] == EditOp::KEEP) {
        keepCount++;
        peekIdx++;
        peekOrigIdx++;
        peekNewIdx++;
      }

      if (keepCount == 0) {
        // No more KEEPs, we're done with this diff or at end
        break;
      } else if (keepCount >= MIN_MATCH_LENGTH || peekIdx >= ops.size()) {
        // Long enough match or end of ops - finalize this diff
        // (we also finalize at end to not lose trailing matches)
        break;
      } else {
        // Short match - check if it contains word boundaries
        // If so, it represents complete word(s) and should be preserved
        bool containsBoundary = false;
        for (int k = 0; k < keepCount; k++) {
          if (isWordBoundaryChar(startText[origIdx + k])) {
            containsBoundary = true;
            break;
          }
        }
        if (containsBoundary) {
          // Preserve this match - it contains word structure
          break;
        }
        // Short match with no boundaries - absorb it into the current diff
        // Treat the kept chars as deleted from source and inserted from target
        for (int k = 0; k < keepCount; k++) {
          deleted += startText[origIdx];
          inserted += endText[newIdx];
          origIdx++;
          newIdx++;
          opIdx++;
        }
        // Continue collecting more changes
      }
    }

    // Only create a diff if there's actually something to change
    if (!deleted.empty() || !inserted.empty()) {
      DiffState diff;
      diff.deletedText = deleted;
      diff.insertedText = inserted;

      // Compute position bounds
      diff.posBegin = flatIndexToPosition(startOrigIdx, startText);

      // posEnd is inclusive, so it's the position of the last deleted char
      // For pure insertions, posEnd == posBegin (insertion point)
      if (deleted.empty()) {
        diff.posEnd = diff.posBegin;
      } else {
        diff.posEnd = flatIndexToPosition(startOrigIdx + static_cast<int>(deleted.size()) - 1, startText);
      }

      // Compute EditBoundary
      diff.boundary = computeEditBoundary(startLines, diff.posBegin, diff.posEnd, deleted);

      result.push_back(std::move(diff));
    }

    // Skip the long KEEP run we found (or remaining KEEPs at end)
    while (opIdx < ops.size() && ops[opIdx] == EditOp::KEEP) {
      origIdx++;
      newIdx++;
      opIdx++;
    }
  }

  return result;
}

Lines applyDiffState(const DiffState& diff, const Lines& lines) {
  // Flatten, apply change, unflatten
  string text = lines.flatten();

  // Find the flat indices for the edit region
  int startIdx = positionToFlatIndex(diff.posBegin, lines);
  int endIdx;

  if (diff.deletedText.empty()) {
    // Pure insertion: insert at startIdx
    endIdx = startIdx;
  } else {
    // Delete from startIdx to startIdx + deletedText.size()
    endIdx = startIdx + static_cast<int>(diff.deletedText.size());
  }

  // Apply the edit
  string newText = text.substr(0, startIdx) + diff.insertedText + text.substr(endIdx);

  return Lines::unflatten(newText);
}

vector<DiffState> adjustForSequential(const vector<DiffState>& diffs) {
  vector<DiffState> result;
  result.reserve(diffs.size());

  // Track cumulative offset in characters
  int charOffset = 0;

  for (const auto& diff : diffs) {
    DiffState adjusted = diff;

    // Adjust positions based on cumulative character offset
    // This is more complex for character-level diffs because
    // we need to track both line and column changes

    // For simplicity, we'll track the offset and recompute positions
    // when applying. The key insight is that each diff changes the
    // buffer by: insertedText.size() - deletedText.size() characters.

    // We need to adjust posBegin and posEnd based on previous diffs.
    // This requires tracking how many newlines were added/removed.

    // Count newlines in deleted vs inserted
    int deletedNewlines = count(diff.deletedText.begin(), diff.deletedText.end(), '\n');
    int insertedNewlines = count(diff.insertedText.begin(), diff.insertedText.end(), '\n');
    int lineOffset = insertedNewlines - deletedNewlines;

    // For now, use a simpler approach: adjust line numbers only
    // This works when diffs don't overlap within a line
    // (which is guaranteed by our sequential processing)

    // Actually, for character-level diffs that may be mid-line,
    // we need to be more careful. Let's track the cumulative effect.

    // Simplified: just adjust line numbers based on previous diffs
    // The column adjustment is complex and depends on whether
    // the previous diff was on the same line.

    // For the MVP, we'll use line-based adjustment similar to before
    // This may need refinement for complex multi-line edits.

    // We'll store the original positions and let applyDiffState
    // handle the actual application correctly.

    result.push_back(std::move(adjusted));
  }

  // For now, return as-is. The sequential application in
  // calculateLinesAfterDiffs handles this correctly by
  // applying each diff to the result of the previous.
  return diffs;
}

Lines applyAllDiffState(const vector<DiffState>& diffs, const Lines& startLines) {
  // Compute all flat indices upfront using the ORIGINAL text positions
  string text = startLines.flatten();

  // Build a map from Position to flat index for the original text
  auto posToFlatIdx = [&](const Position& pos) -> int {
    int idx = 0;
    int line = 0, col = 0;
    for (size_t j = 0; j < text.size(); j++) {
      if (line == pos.line && col == pos.col) {
        return static_cast<int>(j);
      }
      if (text[j] == '\n') {
        line++;
        col = 0;
      } else {
        col++;
      }
    }
    // Position at end of text
    if (line == pos.line && col == pos.col) {
      return static_cast<int>(text.size());
    }
    return static_cast<int>(text.size());
  };

  // Pre-compute all flat indices for all diffs (using original positions)
  vector<pair<int, int>> indices;  // (startIdx, endIdx) for each diff
  for (const auto& diff : diffs) {
    int startIdx = posToFlatIdx(diff.posBegin);
    int endIdx = startIdx;
    if (!diff.deletedText.empty()) {
      endIdx = startIdx + static_cast<int>(diff.deletedText.size());
    }
    indices.emplace_back(startIdx, endIdx);
  }

  // Apply diffs in reverse order (end to start) so that earlier positions
  // remain valid after later edits.
  for (int i = static_cast<int>(diffs.size()) - 1; i >= 0; i--) {
    const DiffState& diff = diffs[i];
    int startIdx = indices[i].first;
    int endIdx = indices[i].second;

    // Bounds check
    if (startIdx > static_cast<int>(text.size())) startIdx = static_cast<int>(text.size());
    if (endIdx > static_cast<int>(text.size())) endIdx = static_cast<int>(text.size());

    // Apply the edit
    text = text.substr(0, startIdx) + diff.insertedText + text.substr(endIdx);
  }

  return Lines::unflatten(text);
}

} // namespace Myers
