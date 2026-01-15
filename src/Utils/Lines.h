#pragma once

#include <memory>
#include <string>
#include <vector>
#include <assert.h>
#include <Editor/Position.h>

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

  Position getLastPos() const {
    assert(!this->empty());
    for(int i = this->size() - 1; i >= 0; i--) {
      if(!(*this)[i].empty()) return Position(i, (*this)[i].size()-1);
    }
    assert(false);
  }

  Position getNextPos(Position pos) const {
    if(pos.col + 1 < (*this)[pos.line].size()) {
      return Position(pos.line, pos.col + 1);
    }
    for(int row = pos.line + 1; row < this->size(); ++row) {
      if(!(*this)[row].empty()) {
        return Position(row, 0);
      }
    }
    return pos; // Same position if cannot move, or check == pos at call site for sentinel
  }

  Position getPrevPos(Position pos) const {
    if(pos.col > 0) {
      return Position(pos.line, pos.col - 1);
    }
    for(int row = pos.line - 1; row >= 0; --row) {
      if(!(*this)[row].empty()) {
        return Position(row, (*this)[row].size() - 1);
      }
    }
    return pos; // Same position if cannot move, or check == pos at call site for sentinel
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
