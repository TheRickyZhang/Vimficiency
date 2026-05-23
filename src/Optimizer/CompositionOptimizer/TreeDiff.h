#pragma once

#include <array>
#include <string>
#include <vector>

#include "DiffState.h"
#include "Types/Lines.h"

namespace DiffAlgorithm {

inline constexpr int Myers = 0;
inline constexpr int Tree = 1;

const char* name(int algorithm);

} // namespace DiffAlgorithm

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

constexpr Level operator+(Level level, int delta) {
  return static_cast<Level>(static_cast<int>(level) + delta);
}
constexpr Level operator-(Level level, int delta) {
  return static_cast<Level>(static_cast<int>(level) - delta);
}


// Character range in the flattened buffer
struct TextRange {
  int begin = 0;
  int end = 0;
};

// Index range in the child level vector this corresponds to
struct ChildRange {
  int begin = 0;
  int end = 0;
};

struct Node {
  TextRange text;
  ChildRange children;
};

struct Tree {
  explicit Tree(const Lines& lines);

  const std::string text;
  std::array<std::vector<Node>, LEVEL_COUNT> levels;

  std::string toString() const;

  const std::vector<Node>& operator[](Level level) const {
    return levels[static_cast<int>(level)];
  }

  int size(Level level) const {
    return static_cast<int>(levels[static_cast<int>(level)].size());
  }

  std::vector<Node>& operator[](Level level) {
    return levels[static_cast<int>(level)];
  }

  int addNode(Level level, TextRange text, ChildRange children = {}) {
    auto& nodes = (*this)[level];
    const int index = static_cast<int>(nodes.size());
    nodes.push_back(Node{.text = text, .children = children});
    return index;
  }
};

std::vector<DiffState> calculate(
    const Lines& initialLines,
    const Lines& goalLines);

} // namespace TreeDiff
