#pragma once

#include <array>
#include <iosfwd>
#include <string>
#include <vector>

#include "Types/Lines.h"

namespace TreeDiff {

enum class Level {
  Root,
  Paragraph,
  Line,
  BigWord,
  Word,
  Char
};
inline constexpr int LEVEL_COUNT = 6;

constexpr int levelIndex(Level level) {
  return static_cast<int>(level);
}

constexpr Level levelAt(int index) {
  return static_cast<Level>(index);
}

constexpr Level operator+(Level level, int delta) {
  return levelAt(levelIndex(level) + delta);
}
constexpr Level operator-(Level level, int delta) {
  return levelAt(levelIndex(level) - delta);
}

const char* levelName(Level level);

struct Tree {
  struct TextRange {
    int begin = 0;
    int end = 0;
    bool empty() const { return begin == end; }
  };

  struct ChildRange {
    int begin = 0;
    int end = 0;
  };

  struct Node {
    TextRange text;
    ChildRange children;
  };

  explicit Tree(const Lines& lines);

  const std::string text;
  std::array<std::vector<Node>, LEVEL_COUNT> levels;

  const std::vector<Node>& operator[](int level) const {
    return levels[level];
  }
  std::vector<Node>& operator[](int level) {
    return levels[level];
  }

  const std::vector<Node>& operator[](Level level) const {
    return (*this)[levelIndex(level)];
  }
  std::vector<Node>& operator[](Level level) {
    return (*this)[levelIndex(level)];
  }

  int size(int level) const {
    return static_cast<int>((*this)[level].size());
  }
  int size(Level level) const {
    return size(levelIndex(level));
  }

  int addNode(Level level, TextRange text, ChildRange children);
};

std::ostream& operator<<(std::ostream& out, const Tree& tree);

}  // namespace TreeDiff
