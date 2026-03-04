#pragma once

#include <algorithm>
#include <cassert>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "Types/CharRange.h"
#include "Types/CharLineRange.h"
#include "Types/LineCharRange.h"
#include "Types/CursorPos.h"

struct Line final : std::string {
  using std::string::string;

  int effectiveSize() const { return empty() ? 1 : static_cast<int>(size()); }
  int lastCol() const { return effectiveSize() - 1; }

  char get(int col) const {
    if (empty()) {
      assert(col == 0 && "col must be 0 in empty line)");
      return '\n';
    }
    return data()[col];
  }

  Line(const std::string& s) : std::string(s) {}
  Line(std::string&& s) : std::string(std::move(s)) {}

  void* operator new(size_t) = delete;
  void* operator new[](size_t) = delete;
};

struct Lines final : std::vector<Line> {
  using std::vector<Line>::vector;

  Lines(std::vector<std::string> v) {
    assert(!v.empty());
    reserve(v.size());
    for (auto& s : v) {
      push_back(std::move(s));
    }
  }

  std::string flatten() const {
    size_t total = 0;
    for (size_t i = 0; i < size(); i++) {
      if (i > 0) {
        total++;
      }
      total += (*this)[i].size();
    }
    std::string result;
    result.reserve(total);
    for (size_t i = 0; i < size(); i++) {
      if (i > 0) {
        result += '\n';
      }
      result += (*this)[i];
    }
    return result;
  }

  static Lines unflatten(std::string_view text) {
    Lines result;
    size_t start = 0;
    size_t pos;
    while ((pos = text.find('\n', start)) != std::string_view::npos) {
      result.push_back(std::string(text.substr(start, pos - start)));
      start = pos + 1;
    }
    result.push_back(std::string(text.substr(start)));
    assert(!result.empty() && "Lines invariant: buffer must have at least one line");
    return result;
  }

  int lastLine() const { return static_cast<int>(size()) - 1; }
  int getSize(int line) const { return static_cast<int>(data()[line].size()); }
  bool isEmpty() const { return size() == 1 && data()[0].empty(); }

  CursorPos getNextPos(CursorPos pos) const {
    if (pos.col + 1 < static_cast<int>((*this)[pos.line].size())) {
      return CursorPos(pos.line, pos.col + 1);
    }
    if (pos.line + 1 < static_cast<int>(size())) {
      return CursorPos(pos.line + 1, 0);
    }
    return pos;
  }

  CursorPos getNextPosUnbounded(CursorPos pos) const {
    if (pos.col + 1 < static_cast<int>((*this)[pos.line].size())) {
      return CursorPos(pos.line, pos.col + 1);
    }
    return CursorPos(pos.line + 1, 0);
  }

  CursorPos getPrevPos(CursorPos pos) const {
    if (pos.col > 0) {
      return CursorPos(pos.line, pos.col - 1);
    }
    if (pos.line > 0) {
      int prevLine = pos.line - 1;
      int prevCol = (*this)[prevLine].empty() ? 0 : static_cast<int>((*this)[prevLine].size()) - 1;
      return CursorPos(prevLine, prevCol);
    }
    return pos;
  }

  CursorPos lastPos() const { return CursorPos(lastLine(), back().lastCol()); }
  CursorPos endPos() const { return CursorPos(lastLine(), back().effectiveSize()); }

  char get(const CursorPos& pos) const {
    assert(pos.line < static_cast<int>(size()) && "Lines::get() position out of bounds");
    return data()[pos.line].get(pos.col);
  }

  Lines getSpan(const CursorPos& begin, const CursorPos& end) const {
    Lines result;
    if (begin.line == end.line) {
      result.push_back(data()[begin.line].substr(begin.col, end.col - begin.col));
    } else {
      result.push_back(data()[begin.line].substr(begin.col));
      for (int i = begin.line + 1; i < end.line; i++) {
        result.push_back(data()[i]);
      }
      result.push_back(data()[end.line].substr(0, end.col));
    }
    return result;
  }

  int spanSize(const CursorPos& begin, const CursorPos& end) const {
    if (begin.line == end.line) {
      return end.col - begin.col;
    }
    int count = std::max(1, static_cast<int>(data()[begin.line].size()) - begin.col);
    for (int i = begin.line + 1; i < end.line; i++) {
      count += std::max(1, static_cast<int>(data()[i].size()));
    }
    count += end.col;
    return count;
  }

  int spanSize(const CharRange& range) const {
    return spanSize(range.begin, range.end);
  }

  int spanSize(const CharLineRange& range) const {
    assert(range.isValid());
    if (range.begin.line + 1 == range.endLine) {
      return std::max(1, static_cast<int>(data()[range.begin.line].size()) - range.begin.col);
    }
    int count = std::max(1, static_cast<int>(data()[range.begin.line].size()) - range.begin.col);
    for (int i = range.begin.line + 1; i < range.endLine; i++) {
      count += std::max(1, static_cast<int>(data()[i].size()));
    }
    return count;
  }

  int spanSize(const LineCharRange& range) const {
    assert(range.isValid());
    return spanSize(CursorPos(range.beginLine, 0), range.end);
  }

  int totalPositions() const {
    int count = 0;
    for (const auto& line : *this) {
      count += line.effectiveSize();
    }
    return count;
  }

  static bool sameLineLengths(const Lines& x, const Lines& y) {
    if (x.size() != y.size()) {
      return false;
    }
    for (size_t i = 0; i < x.size(); i++) {
      if (x[i].size() != y[i].size()) {
        return false;
      }
    }
    return true;
  }

  Lines getLineRange(int beginLine, int endLine) const {
    assert(beginLine >= 0 && "beginLine must be non-negative");
    assert(endLine <= static_cast<int>(size()) && "endLine must be <= size()");
    assert(beginLine < endLine && "must have at least one line in range");
    return Lines(begin() + beginLine, begin() + endLine);
  }

  std::pair<int, int> minmaxBoundWithPadding(int beginLine, int endLine, int padAbove, int padBelow) const {
    if (beginLine > endLine) {
      std::swap(beginLine, endLine);
    }
    return {std::max(0, beginLine - padAbove),
            std::min(static_cast<int>(size()), endLine + padBelow)};
  }

  friend std::ostream& operator<<(std::ostream& os, const Lines& lines) {
    for (const std::string& s : lines) {
      os << s << "\n";
    }
    return os;
  }
};

inline size_t hashLines(const Lines& lines) {
  size_t h = 0xcbf29ce484222325ULL;
  for (const auto& line : lines) {
    for (char c : line) {
      h ^= static_cast<size_t>(static_cast<unsigned char>(c));
      h *= 0x100000001b3ULL;
    }
    h ^= '\n';
    h *= 0x100000001b3ULL;
  }
  h ^= lines.size();
  h *= 0x100000001b3ULL;
  return h;
}

using SharedLines = std::shared_ptr<const Lines>;
