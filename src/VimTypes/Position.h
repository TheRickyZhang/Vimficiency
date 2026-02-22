#pragma once
#include <climits>
#include <ostream>
#include <utility>

struct Position {
  int line = 0;
  int col = 0;
  int targetCol = 0;  // The column we "want" to be at (for sticky column behavior)

private:

public:
  Position() = default;
  constexpr Position(int l, int c) : line(l), targetCol(c), col(c) {}
  constexpr Position(int l, int c, int tc) : line(l), targetCol(tc), col(c) {}

  // Validity check - positions are invalid when line < 0 (sentinel value)
  bool isValid() const { return line >= 0; }

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

  bool operator==(const Position& other) const {
    return line == other.line && col == other.col && targetCol == other.targetCol;
  }
  bool operator!=(const Position& other) const {
    return !(*this == other);
  }
  bool operator<(const Position& other) const {
    if (line != other.line) return line < other.line;
    return col < other.col;
  }
  bool operator>(const Position& other) const {
    return other < *this;
  }
  bool operator<=(const Position& other) const {
    return !(other < *this);
  }
  bool operator>=(const Position& other) const {
    return !(*this < other);
  }
  void swap(Position& other) noexcept {
    std::swap(line, other.line);
    std::swap(col, other.col);
    std::swap(targetCol, other.targetCol);
  }

  friend std::ostream& operator<<(std::ostream& os, const Position& pos) {
    os << "(" << pos.line << ", " << pos.col << ")";
    if(pos.targetCol != pos.col) os << "[" << pos.targetCol << "]" << "\n";
    return os;
  }
};

// Sentinel value for "position outside boundary" / "operation would cross boundary"
// Used by endpoint functions when the computed position would exceed given bounds.
constexpr Position POSITION_OUTSIDE_BOUNDARY{-1, -1, -1};

// Special targetCol value for "end of line" (Vim's curswant after $).
// When targetCol is this value, vertical movements keep cursor at line end.
// clampCol naturally handles this by clamping to len-1.
constexpr int TARGETCOL_EOL = INT_MAX;
