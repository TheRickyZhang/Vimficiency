#pragma once
#include <climits>
#include <utility>
#include "Types/Pos.h"

// Adds targetCol for full vim-state cursor position representation
struct CursorPos : Pos {
  int targetCol = 0;  // The column we "want" to be at (for sticky column behavior)

  CursorPos() = default;
  constexpr CursorPos(int l, int c) : Pos(l, c), targetCol(c) {}
  constexpr CursorPos(int l, int c, int tc) : Pos(l, c), targetCol(tc) {}

  // ==========================================================================
  // Column Assignment Methods
  // ==========================================================================
  //
  // setCol(): Use for ALL horizontal position changes that establish a new
  //           target position. Updates both col and targetCol.
  //   - Insert mode entry (i, I, a, A)
  //   - After character deletions (d, x, etc.)
  //   - Line joins (J)
  //   - Any operation that establishes a new horizontal position
  //
  // clampColPreservingTarget(): Use ONLY for vertical movements that preserve
  //           the "sticky column" behavior. Updates col but keeps targetCol.
  //   - j/k movements
  //   - gg/G line jumps
  //   - C-d/C-u/C-f/C-b scroll commands
  //   - Insert mode <Up>/<Down>
  //
  // The targetCol field remembers where we "want" to be horizontally.
  // Vertical movements clamp to line length but keep targetCol unchanged,
  // so moving through short lines and back to long lines returns to the
  // original column.
  // ==========================================================================

  void setCol(int c) {
    col = targetCol = c;
  }

  // For vertical movements: clamp column to line bounds but preserve targetCol
  void clampColPreservingTarget(int clampedCol) {
    col = clampedCol;
  }

  // Explicit conversion to Pos (drops targetCol).
  // Prefer this over implicit slicing when the intent is to discard targetCol.
  constexpr Pos pos() const { return {line, col}; }

  void swap(CursorPos& other) noexcept {
    std::swap(line, other.line);
    std::swap(col, other.col);
    std::swap(targetCol, other.targetCol);
  }

  friend std::ostream& operator<<(std::ostream& os, const CursorPos& pos) {
    os << "(" << pos.line << ", " << pos.col << ")";
    if(pos.targetCol != pos.col) os << "[" << pos.targetCol << "]" << "\n";
    return os;
  }
};

// Sentinel value for "position outside boundary" / "operation would cross boundary"
// Used by endpoint functions when the computed position would exceed given bounds.
constexpr CursorPos POSITION_OUTSIDE_BOUNDARY{-1, -1, -1};

// Special targetCol value for "end of line" (Vim's curswant after $).
// When targetCol is this value, vertical movements keep cursor at line end.
// clampCol naturally handles this by clamping to len-1.
constexpr int TARGETCOL_EOL = INT_MAX;
