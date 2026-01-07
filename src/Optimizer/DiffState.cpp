#include "DiffState.h"

#include <algorithm>
#include <cassert>

using namespace std;

namespace Myers {

// Minimum length for a common substring to be preserved as a separate match.
// Shorter matches are merged into adjacent diffs to produce more intuitive results.
// e.g., "world" -> "there" shares 'r' (len 1), but we want 1 diff, not 2 (wo->the, d->e).
// But "migration" -> "arbitrations" shares "ration" (len 6), so 2 diffs makes sense.
const int MIN_MATCH_LENGTH = 4;

// Check if a character is a word boundary (whitespace or punctuation) as an exception to above
// Being surrounded by spaces, for instance, is very distinguishable.
// e.g., " b " in "a b c" -> "d b e" should be preserved even though " b " is only 3 < MIN_MATCH_LENGTH chars.
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
// Myers O(ND) Diff Algorithm
// =============================================================================
//
// This implements Eugene Myers' "An O(ND) Difference Algorithm" (1986).
// Same as used in Git diffs, but character-wise, so preserves intuitive "diff" notions.
// Key properties:
// - O(ND) time where N = n + m and D = edit distance (number of edits)
// - Greedy: consumes matches as early as possible during forward search
// - Prefers deletions over insertions on ties (deletions appear first)
// - Produces minimal edit scripts
//
// The algorithm explores an edit graph where:
// - x-axis = source string positions, y-axis = target string positions
// - Moving right (+x) = DELETE from source
// - Moving down (+y) = INSERT from target
// - Diagonal (+x, +y) = KEEP (match, free move)
// - Goal: find shortest path from (0,0) to (n,m)

// Internal: represents an edit operation
enum class EditOp { KEEP, DELETE, INSERT };

// Traceback from the saved history to reconstruct edit operations
static vector<EditOp> myersTraceback(
    const vector<vector<int>>& trace,
    int d_final, int n, int m, int offset,
    const string& a, const string& b) {

  vector<EditOp> ops;
  int x = n, y = m;

  // Work backwards from d_final to d=1
  for (int d = d_final; d > 0; d--) {
    int k = x - y;
    const vector<int>& V_prev = trace[d];

    // Determine which direction we came from
    int prev_k;
    bool was_insert;

    if (k == -d || (k != d && V_prev[k - 1 + offset] < V_prev[k + 1 + offset])) {
      // Came from k+1 (insert: moved down)
      prev_k = k + 1;
      was_insert = true;
    } else {
      // Came from k-1 (delete: moved right)
      prev_k = k - 1;
      was_insert = false;
    }

    // Position at end of previous round on prev_k diagonal
    int end_x = V_prev[prev_k + offset];
    int end_y = end_x - prev_k;

    // Position right after the edit (before snake/diagonal extension)
    int snake_start_x = was_insert ? end_x : end_x + 1;
    int snake_start_y = was_insert ? end_y + 1 : end_y;

    // Add diagonal moves (snake) that happened after the edit
    while (x > snake_start_x && y > snake_start_y) {
      ops.push_back(EditOp::KEEP);
      x--; y--;
    }

    // Add the edit
    if (was_insert) {
      ops.push_back(EditOp::INSERT);
      y--;
    } else {
      ops.push_back(EditOp::DELETE);
      x--;
    }
  }

  // d=0: initial diagonal moves from (0,0) before any edits
  while (x > 0 && y > 0) {
    ops.push_back(EditOp::KEEP);
    x--; y--;
  }

  // Reverse to get forward order
  reverse(ops.begin(), ops.end());
  return ops;
}

// Myers O(ND) diff algorithm - finds shortest edit script
static vector<EditOp> tracePath(const string& a, const string& b) {
  int n = static_cast<int>(a.size());
  int m = static_cast<int>(b.size());

  // Handle edge cases
  if (n == 0 && m == 0) return {};
  if (n == 0) return vector<EditOp>(m, EditOp::INSERT);
  if (m == 0) return vector<EditOp>(n, EditOp::DELETE);

  int max_d = n + m;
  int offset = max_d + 1;  // To handle negative diagonal indices
  int V_size = 2 * max_d + 3;

  // V[k + offset] = furthest x position reached on diagonal k
  // Diagonal k means: x - y = k
  vector<int> V(V_size, 0);
  vector<vector<int>> trace;  // History for traceback

  // Sentinel: V[1] = 0 allows the algorithm to start cleanly
  V[1 + offset] = 0;

  for (int d = 0; d <= max_d; d++) {
    // Save V at the start of this round for traceback
    trace.push_back(V);

    for (int k = -d; k <= d; k += 2) {
      int x;

      // Choose: come from diagonal k-1 (delete) or k+1 (insert)
      // - k == -d: must come from k+1 (can't go further left)
      // - k == d: must come from k-1 (can't go further right)
      // - Otherwise: choose the one that reaches further
      // - Prefer delete (k-1) on tie for consistent output
      if (k == -d || (k != d && V[k - 1 + offset] < V[k + 1 + offset])) {
        x = V[k + 1 + offset];      // insert: come from k+1, x stays same
      } else {
        x = V[k - 1 + offset] + 1;  // delete: come from k-1, x increases
      }

      int y = x - k;

      // Snake: greedily follow diagonal (matches) as far as possible
      while (x < n && y < m && a[x] == b[y]) {
        x++; y++;
      }

      V[k + offset] = x;

      // Check if we've reached the goal
      if (x >= n && y >= m) {
        return myersTraceback(trace, d, n, m, offset, a, b);
      }
    }
  }

  // Should never reach here for valid input
  return {};
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
      }

      // Rule 0: Cross-line matches with little content are likely coincidental
      // e.g., "\n  r" matching just because both files have "return" at same indent
      // This applies even to "long" matches since "\n   x" is 5 chars but weak
      bool containsNewline = false;
      int nonWhitespaceCount = 0;
      for (int k = 0; k < keepCount; k++) {
        char c = startText[origIdx + k];
        if (c == '\n') containsNewline = true;
        if (!std::isspace(static_cast<unsigned char>(c))) nonWhitespaceCount++;
      }
      bool atEnd = (peekIdx >= ops.size());
      // Require at least 3 non-whitespace chars for cross-line matches to be meaningful
      if (containsNewline && nonWhitespaceCount < 3 && !atEnd) {
        // Cross-line match with < 2 non-whitespace chars - absorb
        for (int k = 0; k < keepCount; k++) {
          deleted += startText[origIdx];
          inserted += endText[newIdx];
          origIdx++;
          newIdx++;
          opIdx++;
        }
        continue;
      }

      if (keepCount >= MIN_MATCH_LENGTH) {
        // Long enough match - finalize this diff
        break;
      } else {
        // Short match - decide whether to preserve or absorb
        bool isPureInsertion = deleted.empty();
        bool isPureDeletion = inserted.empty();

        // Check if match contains any boundary chars
        bool hasBoundary = false;
        for (int k = 0; k < keepCount; k++) {
          if (isWordBoundaryChar(startText[origIdx + k])) {
            hasBoundary = true;
            break;
          }
        }

        // Rule 1: At end with boundary - preserve (e.g., trailing ")")
        if (atEnd && hasBoundary) {
          break;
        }

        // Rule 2: Pure insert/delete at end - preserve to keep diff minimal
        if (atEnd && (isPureInsertion || isPureDeletion)) {
          break;
        }

        // Rule 3: Proper boundary+content+boundary - preserve (e.g., " b ")
        if (hasBoundary) {
          bool startsWithBoundary = isWordBoundaryChar(startText[origIdx]);
          bool endsWithBoundary = isWordBoundaryChar(startText[origIdx + keepCount - 1]);
          bool hasContent = false;
          for (int k = 0; k < keepCount; k++) {
            if (!isWordBoundaryChar(startText[origIdx + k])) {
              hasContent = true;
              break;
            }
          }
          if (startsWithBoundary && endsWithBoundary && hasContent) {
            break;
          }
        }

        // Rule 4: Small diff at end - preserve to keep diff minimal
        if (atEnd) {
          int currentDiffSize = static_cast<int>(deleted.size() + inserted.size());
          if (currentDiffSize <= keepCount) {
            break;
          }
        }

        // Absorb - treat the kept chars as deleted from source and inserted from target
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
