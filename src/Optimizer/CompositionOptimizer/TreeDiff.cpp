#include "TreeDiff.h"

#include <array>
#include <cassert>
#include <string>
#include <string_view>
#include <vector>

#include "VimCore/CharMask.h"

using namespace std;
namespace DiffAlgorithm {

const char* name(int algorithm) {
  switch (algorithm) {
    case Myers:
      return "myers";
    case Tree:
      return "tree";
    default:
      return "unknown";
  }
}

} // namespace DiffAlgorithm

namespace TreeDiff {
namespace {

Level childLevel(Level level) {
  assert(level != Level::Char);
  return static_cast<Level>(static_cast<int>(level) + 1);
}

vector<VimCore::CharMask> classifyText(const string& text) {
  vector<VimCore::CharMask> masks;
  masks.reserve(text.size());
  for (char c : text) {
    masks.emplace_back(c);
  }
  return masks;
}

const char* levelName(Level level) {
  switch (level) {
    case Level::Root:
      return "Root";
    case Level::Paragraph:
      return "Paragraph";
    case Level::Line:
      return "Line";
    case Level::BigWord:
      return "BigWord";
    case Level::Word:
      return "Word";
    case Level::Char:
      return "Char";
  }
  return "Unknown";
}

string escapedText(string_view text) {
  string result;
  for (char c : text) {
    switch (c) {
      case '\n':
        result += "\\n";
        break;
      case '\t':
        result += "\\t";
        break;
      case '\\':
        result += "\\\\";
        break;
      default:
        result += c;
        break;
    }
  }
  return result;
}

string nodeText(const Tree& tree, const Node& node) {
  string result = "[";
  result += escapedText(string_view(tree.text).substr(
      static_cast<size_t>(node.text.begin),
      static_cast<size_t>(node.text.end - node.text.begin)));
  result += "]";
  return result;
}

ChildRange childRangeFrom(const Tree& tree, Level level, int childBegin) {
  return ChildRange{
      .begin = childBegin,
      .end = static_cast<int>(tree[childLevel(level)].size()),
  };
}

struct ActiveBegins {
  int textBegin = 0;
  int childBegin = 0;
};

struct ScanState {
  array<ActiveBegins, LEVEL_COUNT> range;
  bool lineHasAnyChar = false;
  bool previousLineWasEmpty = false;

  ActiveBegins& operator[](Level level) {
    return range[static_cast<int>(level)];
  }

  const ActiveBegins& operator[](Level level) const {
    return range[static_cast<int>(level)];
  }
};

DiffState wholeBufferDiff(const Lines& initialLines,
                          const Tree& initialTree,
                          const Tree& goalTree) {
  CursorPos begin = DiffText::flatIndexToPosition(0, initialTree.text);
  CursorPos end = DiffText::advancePositionByText(begin, initialTree.text);

  return DiffState(
      begin, end,
      initialTree.text,
      goalTree.text,
      TransformBoundary(initialLines, begin, end));
}

} // namespace

string Tree::toString() const {
  string result;
  constexpr Level levelsToPrint[] = {
      Level::Root,
      Level::Paragraph,
      Level::Line,
      Level::BigWord,
      Level::Word,
      Level::Char,
  };

  for (Level level : levelsToPrint) {
    result += levelName(level);
    result += ": ";
    const auto& nodes = (*this)[level];
    for (int i = 0; i < static_cast<int>(nodes.size()); i++) {
      if (i > 0) result += " ";
      result += nodeText(*this, nodes[i]);
    }
    result += '\n';
  }

  return result;
}

Tree::Tree(const Lines& lines) : text(lines.flatten()) {
  int sz = static_cast<int>(text.size());

  // efficiency improvements to other levels may be possible, but need further inspection
  (*this)[Level::Root].reserve(1);
  (*this)[Level::Char].reserve(sz);

  const vector<VimCore::CharMask> masks = classifyText(text);
  ScanState state{};

  auto closeSpanAt = [&](Level level, int textEnd) {
    auto& [textBegin, childBegin] = state[level];
    int childEnd = size(level + 1);

    // No-op when this level has not accumulated text.
    [[unlikely]]
    if(textBegin == textEnd) return;

    this->addNode(
        level,
        TextRange{textBegin, textEnd},
        ChildRange{childBegin, childEnd});
    textBegin = textEnd;
    childBegin = childEnd;
  };

  for (int i = 0; i < sz; i++) {
    VimCore::CharMask curr = masks[i];

    if (i > 0) {
      VimCore::CharMask prev = masks[i-1];
      if(prev.newLine()) {
        closeSpanAt(Level::Word, i);
        closeSpanAt(Level::BigWord, i);
        closeSpanAt(Level::Line, i);
        if(state.previousLineWasEmpty && !curr.newLine()) {
          closeSpanAt(Level::Paragraph, i);
        }
      } else {
        if(VimCore::beginsWordBroad(prev, curr)) {
          closeSpanAt(Level::Word, i);
        }
        if(VimCore::beginsBigWordBroad(prev, curr)) {
          closeSpanAt(Level::BigWord, i);
        }
      }
    }

    addNode(Level::Char, TextRange{i, i + 1});

    // Adjust running flags
    if(curr.newLine()) {
      state.previousLineWasEmpty = !state.lineHasAnyChar;
      state.lineHasAnyChar = false;
    } else {
      state.lineHasAnyChar = true;
    }
  }

  // Close any that are still open. These might be no-op, but that should be caught in closeSpanAt early return.
  closeSpanAt(Level::Word, sz);
  closeSpanAt(Level::BigWord, sz);
  closeSpanAt(Level::Line, sz);
  closeSpanAt(Level::Paragraph, sz);

  addNode(Level::Root, TextRange{0, sz}, ChildRange{0, size(Level::Root + 1)});
}

vector<DiffState> calculate(const Lines& initialLines, const Lines& goalLines) {
  Tree initialTree(initialLines);
  Tree goalTree(goalLines);

  return {wholeBufferDiff(initialLines, initialTree, goalTree)};
}

} // namespace TreeDiff
