#include "Tree.h"

#include <iterator>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "Utils/PrettyText.h"
#include "VimCore/CharMask.h"

using namespace std;

namespace TreeDiff {

const char* levelName(Level level) {
  switch (level) {
    case Level::Root:      return "Root";
    case Level::Paragraph: return "Paragraph";
    case Level::Line:      return "Line";
    case Level::BigWord:   return "BigWord";
    case Level::Word:      return "Word";
    case Level::Char:      return "Char";
  }
  return "Unknown";
}

namespace {

constexpr int VISUAL_LEVEL_COUNT = levelIndex(Level::Char);

vector<VimCore::CharMask> classifyText(const string& text) {
  vector<VimCore::CharMask> masks;
  masks.reserve(text.size());
  for (char c : text) {
    masks.emplace_back(c);
  }
  return masks;
}

struct ActiveBegins {
  int textBegin = 0;
  int childBegin = 0;
  bool hasCore = false;
};

struct ScanState {
  array<ActiveBegins, LEVEL_COUNT> range;
  bool lineHasNonWhitespace = false;
  bool previousLineWasEmpty = false;

  ActiveBegins& operator[](Level level) {
    return range[levelIndex(level)];
  }

  const ActiveBegins& operator[](Level level) const {
    return range[levelIndex(level)];
  }
};

}  // namespace

ostream& operator<<(ostream& out, const Tree& tree) {
  out << "  counts:";
  for (int i = 0; i < LEVEL_COUNT; i++) {
    const Level level = levelAt(i);
    out << " " << levelName(level) << "=" << tree.size(level);
  }
  out << "\n";

  const int n = ssize(tree.text);
  vector<VF::PrettyText> glyph(n);
  for (int i = 0; i < n; i++) {
    glyph[i] = VF::PrettyText(tree.text[i]);
  }
  if (n == 0) return out;

  for (int lev = 0; lev < VISUAL_LEVEL_COUNT; lev++) {
    vector<bool> dividerAt(n + 1, false);
    for (const auto& node : tree.levels[lev]) {
      if (node.text.begin > 0) dividerAt[node.text.begin] = true;
      if (node.text.end < n) dividerAt[node.text.end] = true;
    }

    out << "  " << VF::PrettyText::TREE_OPEN;
    for (int i = 0; i < n; i++) {
      if (i > 0) {
        out << (dividerAt[i] ? VF::PrettyText::TREE_DIVIDER : string_view(" "));
      }
      out << glyph[i];
    }
    out << VF::PrettyText::TREE_CLOSE << "\n";
  }
  return out;
}

int Tree::addNode(Level level, TextRange text, ChildRange children) {
  auto& nodes = (*this)[level];
  const int index = ssize(nodes);
  nodes.push_back(Node{.text = text, .children = children});
  return index;
}

Tree::Tree(const Lines& lines) : text(lines.flatten()) {
  const int sz = ssize(text);

  (*this)[Level::Root].reserve(1);
  (*this)[Level::Char].reserve(sz);

  const vector<VimCore::CharMask> masks = classifyText(text);
  ScanState state{};

  auto closeSpanAt = [&](Level level, int textEnd) {
    ActiveBegins& active = state[level];
    auto& [textBegin, childBegin, hasCore] = active;
    const int childEnd = size(level + 1);

    [[unlikely]]
    if (textBegin == textEnd) return;

    this->addNode(
        level,
        TextRange{textBegin, textEnd},
        ChildRange{childBegin, childEnd});
    textBegin = textEnd;
    childBegin = childEnd;
    hasCore = false;
  };

  for (int i = 0; i < sz; i++) {
    const VimCore::CharMask curr = masks[i];

    if (i > 0) {
      const VimCore::CharMask prev = masks[i - 1];
      if (prev.newLine()) {
        closeSpanAt(Level::Word, i);
        closeSpanAt(Level::BigWord, i);
        closeSpanAt(Level::Line, i);
        if (state.previousLineWasEmpty && !curr.newLine()) {
          closeSpanAt(Level::Paragraph, i);
        }
      } else {
        if (state[Level::Word].hasCore &&
           VimCore::beginsWordBroad(prev, curr)) {
          closeSpanAt(Level::Word, i);
        }
        if (state[Level::BigWord].hasCore &&
           VimCore::beginsBigWordBroad(prev, curr)) {
          closeSpanAt(Level::BigWord, i);
        }
      }
    }

    addNode(Level::Char, TextRange{i, i + 1}, {});

    if (curr.newLine()) {
      state.previousLineWasEmpty = !state.lineHasNonWhitespace;
      state.lineHasNonWhitespace = false;
    } else {
      state.lineHasNonWhitespace = state.lineHasNonWhitespace || curr.bigWord();
      if (curr.bigWord()) {
        state[Level::Word].hasCore = true;
        state[Level::BigWord].hasCore = true;
      }
    }
  }

  closeSpanAt(Level::Word, sz);
  closeSpanAt(Level::BigWord, sz);
  closeSpanAt(Level::Line, sz);
  closeSpanAt(Level::Paragraph, sz);
  if (sz > 0) {
    addNode(Level::Root, TextRange{0, sz}, ChildRange{0, size(Level::Root + 1)});
  }
}

}  // namespace TreeDiff
