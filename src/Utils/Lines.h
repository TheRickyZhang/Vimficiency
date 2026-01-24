#pragma once

#include <memory>
#include <string>
#include <vector>
#include <assert.h>
#include <iostream>
#include <Editor/Position.h>

// Lines represents buffer content as a vector of strings.
//
// INVARIANT: A valid buffer always has at least one line.
// The minimum buffer state is a single empty line: {""}
// This matches Vim's behavior - a buffer always has at least one line.
//
// This invariant should be maintained by all code that creates or modifies Lines.
// Functions may assert(!lines.empty()) rather than handle the impossible case.
struct Line final : std::string {
  using std::string::string; 

  int effectiveSize() const {
    return this->empty() ? 1 : this->size();
  }
  int lastCol() const {
    return effectiveSize() - 1;
  }
  char get(int col) const {
    if(this->empty()) {
      assert(col == 0);
      return '\n';
    } else {
      return data()[col];
    }
  }

  // Allow conversion from std::string
  Line(const std::string& s) : std::string(s) {}
  Line(std::string&& s) : std::string(std::move(s)) {}

  // Just to avoid breaking functionality with inheriting from standard type
  void* operator new(size_t) = delete;
  void* operator new[](size_t) = delete;
};

struct Lines  final : std::vector<Line> {
  using std::vector<Line>::vector;

  Lines(std::vector<std::string> v) {
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


  // Returns index of last line (size - 1). Zero cost when inlined.
  int lastLine() const {
    return static_cast<int>(size()) - 1;
  }

  int getSize(int line) const {
    return this->data()[line].size();
  }

  bool isEmpty() const {
    return size() == 1 && this->data()[0].empty();
  }

  // Get next position, including [0] on empty line.
  // Consider: why is it most sensible to clamp here vs returning sentinel? Where do we do check?
  Position getNextPos(Position pos) const {
    if(pos.col + 1 < (*this)[pos.line].size()) {
      return Position(pos.line, pos.col + 1);
    }
    if(pos.line + 1 < this->size()) {
      return Position(pos.line + 1, 0);
    }
    return pos; // Same position if cannot move
  }


  // Get prev position, including [0] on empty line
  Position getPrevPos(Position pos) const {
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

  Position lastPos() const {
    return Position(size()-1, this->back().lastCol());
  }

  char get(const Position& pos) const {
    assert(pos.line < static_cast<int>(this->size()) && "Lines::get() position out of bounds");
    return data()[pos.line].get(pos.col);
  }

  Lines getSpan(const Position& front, const Position& back) const {
    Lines result;
    if (front.line == back.line) {
      result.push_back(data()[front.line].substr(front.col, back.col - front.col + 1));
    } else {
      result.push_back(data()[front.line].substr(front.col));
      for (int i = front.line + 1; i < back.line; i++) {
        result.push_back(data()[i]);
      }
      result.push_back(data()[back.line].substr(0, back.col + 1));
    }
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

  friend std::ostream& operator<<(std::ostream& os, const Lines& lines) {
    for(const std::string& s : lines) { os << s << "\n"; }
    return os;
  }
};


// Copy-on-write shared Lines for efficient state sharing in A* search.
// Motions share the same buffer (O(1)), edits copy-on-write (O(n)).
using SharedLines = std::shared_ptr<const Lines>;
