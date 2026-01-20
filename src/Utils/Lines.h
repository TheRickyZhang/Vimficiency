#pragma once

#include <memory>
#include <string>
#include <vector>
#include <assert.h>
#include <Editor/Position.h>

// Lines represents buffer content as a vector of strings.
//
// INVARIANT: A valid buffer always has at least one line.
// The minimum buffer state is a single empty line: {""}
// This matches Vim's behavior - a buffer always has at least one line.
//
// This invariant should be maintained by all code that creates or modifies Lines.
// Functions may assert(!lines.empty()) rather than handle the impossible case.
struct Lines : std::vector<std::string> {
  using std::vector<std::string>::vector;

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
    return result;
  }

  static bool sameLineLengths(const Lines& x, const Lines& y) {
    if(x.size() != y.size()) return false;
    for(int i = 0; i < x.size(); i++) {
      if(x[i].size() != y[i].size()) {
        return false;
      }
    }
    return true;
  }

  // Returns index of last line (size - 1). Zero cost when inlined.
  int lastLine() const {
    return static_cast<int>(size()) - 1;
  }

  Position getLastPos() const {
    assert(!this->empty());
    for(int i = this->size() - 1; i >= 0; i--) {
      if(!(*this)[i].empty()) return Position(i, (*this)[i].size()-1);
    }
    assert(false);
  }

  // Get next position, skipping empty lines (for character-by-character traversal)
  Position getNextPos(Position pos) const {
    if(pos.col + 1 < (*this)[pos.line].size()) {
      return Position(pos.line, pos.col + 1);
    }
    for(int row = pos.line + 1; row < this->size(); ++row) {
      if(!(*this)[row].empty()) {
        return Position(row, 0);
      }
    }
    return pos; // Same position if cannot move
  }

  // Get next position, including empty lines (for word motions where empty line = word)
  Position getNextPosIncludeEmpty(Position pos) const {
    if(pos.col + 1 < (*this)[pos.line].size()) {
      return Position(pos.line, pos.col + 1);
    }
    if(pos.line + 1 < this->size()) {
      return Position(pos.line + 1, 0);
    }
    return pos; // Same position if cannot move
  }

  // Get prev position, skipping empty lines
  Position getPrevPos(Position pos) const {
    if(pos.col > 0) {
      return Position(pos.line, pos.col - 1);
    }
    for(int row = pos.line - 1; row >= 0; --row) {
      if(!(*this)[row].empty()) {
        return Position(row, (*this)[row].size() - 1);
      }
    }
    return pos; // Same position if cannot move
  }

  // Get prev position, including empty lines (for word motions where empty line = word)
  Position getPrevPosIncludeEmpty(Position pos) const {
    if(pos.col > 0) {
      return Position(pos.line, pos.col - 1);
    }
    if(pos.line > 0) {
      int prevLine = pos.line - 1;
      int prevCol = (*this)[prevLine].empty() ? 0 : (*this)[prevLine].size() - 1;
      return Position(prevLine, prevCol);
    }
    return pos; // Same position if cannot move
  }

  char get(const Position& pos) const {
    assert(pos.line < this->size());
    // Empty line at col=0 is valid - treat as newline (blank)
    if ((*this)[pos.line].empty()) return '\n';
    assert(pos.col < (*this)[pos.line].size());
    return (*this)[pos.line][pos.col];
  }

  // Total character count (excluding newlines between lines)
  int charCount() const {
    int count = 0;
    for (const auto& line : *this) {
      count += static_cast<int>(line.size());
    }
    return count;
  }

  friend std::ostream& operator<<(std::ostream& os, const Lines& lines) {
    for(const std::string& s : lines) { os << s << "\n"; }
    return os;
  }
};


// Copy-on-write shared Lines for efficient state sharing in A* search.
// Motions share the same buffer (O(1)), edits copy-on-write (O(n)).
using SharedLines = std::shared_ptr<const Lines>;
