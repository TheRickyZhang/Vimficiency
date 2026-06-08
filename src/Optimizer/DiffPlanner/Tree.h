#pragma once

#include <array>
#include <cassert>
#include <iosfwd>
#include <string>
#include <vector>

#include "Types/Lines.h"

namespace DiffTree {

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

constexpr Level childLevel(Level level) {
  assert(level != Level::Char);
  return levelAt(levelIndex(level) + 1);
}

// Keystroke cost of the bare motion to traverse one unit at a given level:
// l/w/j = 1, W/} (shifted) = 2. Root is never a diff unit.
constexpr double levelCost(Level level) {
  switch (level) {
    case Level::Paragraph: return 2.0;  // }
    case Level::Line:      return 1.0;  // j
    case Level::BigWord:   return 2.0;  // W
    case Level::Word:      return 1.0;  // w
    case Level::Char:      return 1.0;  // l
    default:               return 0.0;  // Root
  }
}

// Keystroke cost of one delete command at a given level: the motion plus the `d`
// operator, count-independent. char is `x` (a single key, no operator).
constexpr double deleteCost(Level level) {
  return level == Level::Char ? 1.0 : levelCost(level) + 1.0;
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

}  // namespace DiffTree
