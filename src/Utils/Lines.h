#pragma once

#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <Editor/Position.h>

// Line extends std::string with cursor-aware accessors.

// Use effectiveSize() for cursor position bounds (returns 1 for empty lines).
// Use size() for raw character count (returns 0 for empty lines).
struct Line final : std::string {
  using std::string::string;

  // Cursor-aware size: empty lines still have one valid cursor position (col 0)
  int effectiveSize() const { return empty() ? 1 : static_cast<int>(size()); }
  int lastCol() const { return effectiveSize() - 1; }

  char get(int col) const {
    if (empty()) {
      assert(col == 0 && "col must be 0 in empty line)");
      return '\n';
    }
    return data()[col];
  }

  // Allow conversion from std::string
  Line(const std::string& s) : std::string(s) {}
  Line(std::string&& s) : std::string(std::move(s)) {}

  // Prevent heap allocation of Line objects (use std::string if needed)
  void* operator new(size_t) = delete;
  void* operator new[](size_t) = delete;
};

// Lines represents buffer content as a vector of Line objects.
//
// INVARIANT: A valid buffer always has at least one line.
// The minimum buffer state is a single empty line: {""}
// This matches Vim's behavior - a buffer always has at least one line.
struct Lines final : std::vector<Line> {
  using std::vector<Line>::vector;

  Lines(std::vector<std::string> v) {
    assert(!v.empty());
    reserve(v.size());
    for (auto& s : v) push_back(std::move(s));
  }

  std::string flatten() const {
    std::string result;
    for (size_t i = 0; i < size(); i++) {
      if (i > 0)
        result += '\n';
      result += (*this)[i];
    }
    return result;
  }

  static Lines unflatten(const std::string& text) {
    Lines result;
    size_t start = 0;
    size_t pos;
    while ((pos = text.find('\n', start)) != std::string::npos) {
      result.push_back(text.substr(start, pos - start));
      start = pos + 1;
    }
    result.push_back(text.substr(start));
    assert(!result.empty() && "Lines invariant: buffer must have at least one line");
    return result;
  }

  // Returns index of last line (size - 1)
  int lastLine() const { return static_cast<int>(size()) - 1; }

  int getSize(int line) const { return static_cast<int>(data()[line].size()); }

  bool isEmpty() const { return size() == 1 && data()[0].empty(); }

  // Get next position, including [0] on empty line
  Position getNextPos(Position pos) const {
    if (pos.col + 1 < static_cast<int>((*this)[pos.line].size())) {
      return Position(pos.line, pos.col + 1);
    }
    if (pos.line + 1 < static_cast<int>(size())) {
      return Position(pos.line + 1, 0);
    }
    return pos;
  }

  // Get prev position, including [0] on empty line
  Position getPrevPos(Position pos) const {
    if (pos.col > 0) {
      return Position(pos.line, pos.col - 1);
    }
    if (pos.line > 0) {
      int prevLine = pos.line - 1;
      int prevCol = (*this)[prevLine].empty() ? 0 : static_cast<int>((*this)[prevLine].size()) - 1;
      return Position(prevLine, prevCol);
    }
    return pos;
  }

  Position lastPos() const { return Position(lastLine(), back().lastCol()); }

  // Exclusive end: one past the last valid cursor position on the last line
  Position endPos() const { return Position(lastLine(), back().effectiveSize()); }

  char get(const Position& pos) const {
    assert(pos.line < static_cast<int>(size()) && "Lines::get() position out of bounds");
    return data()[pos.line].get(pos.col);
  }

  // Get span of text from front (inclusive) to end (exclusive)
  Lines getSpan(const Position& front, const Position& end) const {
    Lines result;
    if (front.line == end.line) {
      result.push_back(data()[front.line].substr(front.col, end.col - front.col));
    } else {
      result.push_back(data()[front.line].substr(front.col));
      for (int i = front.line + 1; i < end.line; i++) {
        result.push_back(data()[i]);
      }
      result.push_back(data()[end.line].substr(0, end.col));
    }
    return result;
  }

  // Count effective cursor positions in range [front, end) (exclusive end)
  // Empty lines count as 1 position
  int spanSize(const Position& front, const Position& end) const {
    if (front.line == end.line) {
      return end.col - front.col;
    }
    // First line: from front.col to end (or 1 if empty)
    int count = std::max(1, static_cast<int>(data()[front.line].size()) - front.col);
    // Middle lines: full line size (or 1 if empty)
    for (int i = front.line + 1; i < end.line; i++) {
      count += std::max(1, static_cast<int>(data()[i].size()));
    }
    // Last line: from 0 to end.col
    count += end.col;
    return count;
  }

  // Count total effective cursor positions across all lines
  int totalPositions() const {
    int count = 0;
    for (const auto& line : *this) {
      count += line.effectiveSize();
    }
    return count;
  }

  static bool sameLineLengths(const Lines& x, const Lines& y) {
    if (x.size() != y.size())
      return false;
    for (size_t i = 0; i < x.size(); i++) {
      if (x[i].size() != y[i].size()) {
        return false;
      }
    }
    return true;
  }

  // Returns lines [beginLine, endLine)
  Lines getLineRange(int beginLine, int endLine) const {
    assert(beginLine >= 0 && "startLine must be non-negative");
    assert(endLine <= static_cast<int>(size()) && "endLine must be <= size()");
    assert(beginLine < endLine && "must have at least one line in range");
    return Lines(begin() + beginLine, begin() + endLine);
  }

  // Returns [beginLine, endLine)
  std::pair<int, int> minmaxBoundWithPadding(int firstLine, int lastLine, int padBelow, int padAbove) const {
    if(firstLine > lastLine) std::swap(firstLine, lastLine);
    return {
      std::max(0, firstLine - padBelow),
      std::min(static_cast<int>(size()), lastLine + padAbove + 1)
    };
  }

  friend std::ostream& operator<<(std::ostream& os, const Lines& lines) {
    for (const std::string& s : lines) {
      os << s << "\n";
    }
    return os;
  }
};

// Copy-on-write shared Lines for efficient state sharing in A* search.
using SharedLines = std::shared_ptr<const Lines>;
