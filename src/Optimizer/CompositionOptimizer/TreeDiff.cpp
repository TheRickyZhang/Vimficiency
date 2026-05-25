#include "TreeDiff.h"

#include <array>
#include <cassert>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Effort/RunningEffort.h"
#include "Keyboard/KeyedSequence.h"
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
  bool hasCore = false;
};

struct ScanState {
  array<ActiveBegins, LEVEL_COUNT> range;
  bool lineHasNonWhitespace = false;
  bool previousLineWasEmpty = false;

  ActiveBegins& operator[](Level level) {
    return range[static_cast<int>(level)];
  }

  const ActiveBegins& operator[](Level level) const {
    return range[static_cast<int>(level)];
  }
};

struct EditSpan {
  TextRange oldText;
  TextRange newText;
};

struct Plan {
  double cost = numeric_limits<double>::max() / 4.0;
  vector<EditSpan> spans;
};

int textSize(TextRange range) {
  return range.end - range.begin;
}

bool sameTextRange(const Tree& oldTree, TextRange oldRange,
                   const Tree& newTree, TextRange newRange) {
  return string_view(oldTree.text).substr(
             static_cast<size_t>(oldRange.begin),
             static_cast<size_t>(textSize(oldRange))) ==
         string_view(newTree.text).substr(
             static_cast<size_t>(newRange.begin),
             static_cast<size_t>(textSize(newRange)));
}

bool sameText(const Tree& oldTree, const Node& oldNode,
              const Tree& newTree, const Node& newNode) {
  return sameTextRange(oldTree, oldNode.text, newTree, newNode.text);
}

TextRange childTextRange(const Tree& tree, Level parentLevel, const Node& node) {
  if (node.children.begin == node.children.end) {
    return TextRange{node.text.begin, node.text.begin};
  }

  Level nextLevel = childLevel(parentLevel);
  const auto& children = tree[nextLevel];
  return TextRange{
      children[node.children.begin].text.begin,
      children[node.children.end - 1].text.end,
  };
}

bool sameTextOutsideChildren(const Tree& oldTree,
                             const Node& oldNode,
                             const Tree& newTree,
                             const Node& newNode,
                             Level nodeLevel) {
  TextRange oldChildText = childTextRange(oldTree, nodeLevel, oldNode);
  TextRange newChildText = childTextRange(newTree, nodeLevel, newNode);

  return sameTextRange(
             oldTree,
             TextRange{oldNode.text.begin, oldChildText.begin},
             newTree,
             TextRange{newNode.text.begin, newChildText.begin}) &&
         sameTextRange(
             oldTree,
             TextRange{oldChildText.end, oldNode.text.end},
             newTree,
             TextRange{newChildText.end, newNode.text.end});
}

void appendSpans(vector<EditSpan>& target, vector<EditSpan> source) {
  target.insert(
      target.end(),
      make_move_iterator(source.begin()),
      make_move_iterator(source.end()));
}

DiffState diffFromSpan(const Lines& initialLines,
                       const Tree& initialTree,
                       const Tree& goalTree,
                       const EditSpan& span) {
  string deletedText = initialTree.text.substr(
      static_cast<size_t>(span.oldText.begin),
      static_cast<size_t>(textSize(span.oldText)));
  string insertedText = goalTree.text.substr(
      static_cast<size_t>(span.newText.begin),
      static_cast<size_t>(textSize(span.newText)));

  CursorPos begin =
      DiffText::flatIndexToPosition(span.oldText.begin, initialTree.text);
  CursorPos end = DiffText::advancePositionByText(begin, deletedText);

  return DiffState(
      begin,
      end,
      std::move(deletedText),
      std::move(insertedText),
      TransformBoundary(initialLines, begin, end));
}

class Solver {
public:
  Solver(const Tree& oldTree,
         const Tree& newTree,
         const Config& config,
         CostOptions options)
      : oldTree(oldTree),
        newTree(newTree),
        config(config),
        options(options) {}

  Plan solve() {
    return solveChildren(
        Level::Root,
        oldTree[Level::Root].front(),
        newTree[Level::Root].front());
  }

private:
  static constexpr double INF = numeric_limits<double>::max() / 4.0;

  const Tree& oldTree;
  const Tree& newTree;
  const Config& config;
  CostOptions options;

  double typedTextEffort(string_view text) const {
    KeyedSequence typed;
    typed.append(text);
    RunningEffort effort(typed.keys, config);
    return effort.getEffort(config);
  }

  double typedRangeEffort(const Tree& tree, TextRange range) const {
    return typedTextEffort(string_view(tree.text).substr(
        static_cast<size_t>(range.begin),
        static_cast<size_t>(range.end - range.begin)));
  }

  double diffSpanCost(const EditSpan& span) const {
    RunningEffort effort;
    effort.addPenalty(options.diffOpenPenalty);
    KeyedSequence typed;
    typed.append(string_view(newTree.text).substr(
        static_cast<size_t>(span.newText.begin),
        static_cast<size_t>(textSize(span.newText))));
    effort.append(typed.keys, config);
    return effort.getEffort(config);
  }

  struct ListDp {
    enum class OuterChoice {
      Unset,
      Keep,
      Recurse,
      Diff
    };

    struct InnerBuild {
      int nextI = -1;
      int nextJ = -1;
    };

    Solver& solver;
    Level childLevel_;
    const Node& oldParent;
    const Node& newParent;
    int oldBegin;
    int oldEnd;
    int newBegin;
    int newEnd;
    int oldCount;
    int newCount;
    vector<vector<double>> outerCost;
    vector<vector<double>> innerCost;
    vector<vector<OuterChoice>> outerChoice;
    vector<vector<InnerBuild>> innerChoice;
    vector<vector<double>> newRangeEffort;

    ListDp(Solver& solver,
           Level parentLevel,
           const Node& oldParent,
           const Node& newParent)
        : solver(solver),
          childLevel_(childLevel(parentLevel)),
          oldParent(oldParent),
          newParent(newParent),
          oldBegin(oldParent.children.begin),
          oldEnd(oldParent.children.end),
          newBegin(newParent.children.begin),
          newEnd(newParent.children.end),
          oldCount(oldEnd - oldBegin),
          newCount(newEnd - newBegin),
          outerCost(
              static_cast<size_t>(oldCount + 1),
              vector<double>(static_cast<size_t>(newCount + 1), INF)),
          innerCost(
              static_cast<size_t>(oldCount + 1),
              vector<double>(static_cast<size_t>(newCount + 1), INF)),
          outerChoice(
              static_cast<size_t>(oldCount + 1),
              vector<OuterChoice>(
                  static_cast<size_t>(newCount + 1), OuterChoice::Unset)),
          innerChoice(
              static_cast<size_t>(oldCount + 1),
              vector<InnerBuild>(static_cast<size_t>(newCount + 1))),
          newRangeEffort(
              static_cast<size_t>(newCount + 1),
              vector<double>(static_cast<size_t>(newCount + 1), 0.0)) {
      for (int begin = 0; begin <= newCount; begin++) {
        for (int end = begin + 1; end <= newCount; end++) {
          newRangeEffort[static_cast<size_t>(begin)][static_cast<size_t>(end)] =
              solver.typedRangeEffort(
                  solver.newTree,
                  TextRange{boundaryNew(begin), boundaryNew(end)});
        }
      }
    }

    Plan solve() {
      return Plan{
          .cost = outer(0, 0),
          .spans = buildOuter(0, 0),
      };
    }

    double outer(int i, int j) {
      if (i == oldCount && j == newCount) return 0;

      double& res = outerCost[static_cast<size_t>(i)][static_cast<size_t>(j)];
      if (res != INF) return res;

      auto update = [&](double cost, OuterChoice choice) {
        if (cost < res) {
          res = cost;
          outerChoice[static_cast<size_t>(i)][static_cast<size_t>(j)] = choice;
        }
      };

      if (i < oldCount && j < newCount) {
        const Node& oldNode = oldChild(i);
        const Node& newNode = newChild(j);
        if (sameText(solver.oldTree, oldNode, solver.newTree, newNode)) {
          update(outer(i + 1, j + 1), OuterChoice::Keep);
        } else if (childLevel_ != Level::Char &&
                   sameTextOutsideChildren(
                       solver.oldTree, oldNode,
                       solver.newTree, newNode,
                       childLevel_)) {
          Plan child = solver.solveChildren(childLevel_, oldNode, newNode);
          update(child.cost + outer(i + 1, j + 1), OuterChoice::Recurse);
        }
      }

      if (i < oldCount || j < newCount) {
        update(solver.options.diffOpenPenalty + inner(i, j), OuterChoice::Diff);
      }

      return res;
    }

    double inner(int i, int j) {
      if (i == oldCount && j == newCount) return 0;

      double& res = innerCost[static_cast<size_t>(i)][static_cast<size_t>(j)];
      if (res != INF) return res;

      auto update = [&](double cost, int nextI, int nextJ) {
        if (cost < res) {
          res = cost;
          innerChoice[static_cast<size_t>(i)][static_cast<size_t>(j)] =
              InnerBuild{.nextI = nextI, .nextJ = nextJ};
        }
      };

      for (int nextI = i; nextI <= oldCount; nextI++) {
        for (int nextJ = j; nextJ <= newCount; nextJ++) {
          if (nextI == i && nextJ == j) continue;
          double insertedCost =
              newRangeEffort[static_cast<size_t>(j)][static_cast<size_t>(nextJ)];
          update(insertedCost + outer(nextI, nextJ), nextI, nextJ);
        }
      }

      return res;
    }

    vector<EditSpan> buildOuter(int i, int j) {
      if (i == oldCount && j == newCount) return {};

      outer(i, j);
      switch (outerChoice[static_cast<size_t>(i)][static_cast<size_t>(j)]) {
        case OuterChoice::Keep:
          return buildOuter(i + 1, j + 1);

        case OuterChoice::Recurse: {
          Plan child = solver.solveChildren(
              childLevel_, oldChild(i), newChild(j));
          vector<EditSpan> result = std::move(child.spans);
          appendSpans(result, buildOuter(i + 1, j + 1));
          return result;
        }

        case OuterChoice::Diff: {
          InnerBuild build = buildInner(i, j);
          EditSpan span{
              .oldText = TextRange{
                  boundaryOld(i),
                  boundaryOld(build.nextI),
              },
              .newText = TextRange{
                  boundaryNew(j),
                  boundaryNew(build.nextJ),
              },
          };
          vector<EditSpan> result = refinedSpans(i, build.nextI, j, build.nextJ, span);
          appendSpans(result, buildOuter(build.nextI, build.nextJ));
          return result;
        }

        case OuterChoice::Unset:
          assert(false);
          return {};
      }

      assert(false);
      return {};
    }

    InnerBuild buildInner(int i, int j) {
      inner(i, j);
      InnerBuild build = innerChoice[static_cast<size_t>(i)][static_cast<size_t>(j)];
      assert(build.nextI != -1 && build.nextJ != -1);
      return build;
    }

    const Node& oldChild(int offset) const {
      return solver.oldTree[childLevel_][oldBegin + offset];
    }

    const Node& newChild(int offset) const {
      return solver.newTree[childLevel_][newBegin + offset];
    }

    int boundaryOld(int offset) const {
      if (offset == oldCount && oldCount > 0) {
        return oldChild(oldCount - 1).text.end;
      }
      if (offset == oldCount) return oldParent.text.begin;
      return oldChild(offset).text.begin;
    }

    int boundaryNew(int offset) const {
      if (offset == newCount && newCount > 0) {
        return newChild(newCount - 1).text.end;
      }
      if (offset == newCount) return newParent.text.begin;
      return newChild(offset).text.begin;
    }

    vector<EditSpan> refinedSpans(
        int startI,
        int endI,
        int startJ,
        int endJ,
        const EditSpan& span) {
      if (childLevel_ == Level::Char) return {span};

      Plan refined = solver.solveChildren(
          childLevel_,
          Node{.text = span.oldText,
               .children = childRangeForOld(startI, endI)},
          Node{.text = span.newText,
               .children = childRangeForNew(startJ, endJ)});

      double coarseCost = solver.diffSpanCost(span);
      if (!refined.spans.empty() && refined.cost <= coarseCost) {
        return std::move(refined.spans);
      }
      return {span};
    }

    ChildRange childRangeForOld(int start, int end) const {
      if (start == end) return emptyChildRangeForOld(start);
      return ChildRange{
          .begin = oldChild(start).children.begin,
          .end = oldChild(end - 1).children.end,
      };
    }

    ChildRange childRangeForNew(int start, int end) const {
      if (start == end) return emptyChildRangeForNew(start);
      return ChildRange{
          .begin = newChild(start).children.begin,
          .end = newChild(end - 1).children.end,
      };
    }

    ChildRange emptyChildRangeForOld(int offset) const {
      if (offset < oldCount) {
        int childBegin = oldChild(offset).children.begin;
        return ChildRange{.begin = childBegin, .end = childBegin};
      }
      if (oldCount > 0) {
        int childEnd = oldChild(oldCount - 1).children.end;
        return ChildRange{.begin = childEnd, .end = childEnd};
      }
      return {};
    }

    ChildRange emptyChildRangeForNew(int offset) const {
      if (offset < newCount) {
        int childBegin = newChild(offset).children.begin;
        return ChildRange{.begin = childBegin, .end = childBegin};
      }
      if (newCount > 0) {
        int childEnd = newChild(newCount - 1).children.end;
        return ChildRange{.begin = childEnd, .end = childEnd};
      }
      return {};
    }
  };

  Plan solveChildren(Level parentLevel,
                     const Node& oldParent,
                     const Node& newParent) {
    ListDp dp(*this, parentLevel, oldParent, newParent);
    return dp.solve();
  }
};

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
    ActiveBegins& active = state[level];
    auto& [textBegin, childBegin, hasCore] = active;
    int childEnd = size(level + 1);

    // No-op when this level has not accumulated text.
    [[unlikely]]
    if(textBegin == textEnd) return;

    if ((level == Level::Word || level == Level::BigWord) && !hasCore) {
      textBegin = textEnd;
      childBegin = childEnd;
      return;
    }

    this->addNode(
        level,
        TextRange{textBegin, textEnd},
        ChildRange{childBegin, childEnd});
    textBegin = textEnd;
    childBegin = childEnd;
    hasCore = false;
  };

  auto resetSpanAt = [&](Level level, int textBegin) {
    state[level] = ActiveBegins{
        .textBegin = textBegin,
        .childBegin = size(level + 1),
    };
  };

  for (int i = 0; i < sz; i++) {
    VimCore::CharMask curr = masks[i];

    if(curr.newLine()) {
      closeSpanAt(Level::Word, i);
      closeSpanAt(Level::BigWord, i);
    }

    if (i > 0) {
      VimCore::CharMask prev = masks[i-1];
      if(prev.newLine()) {
        closeSpanAt(Level::Line, i);
        resetSpanAt(Level::Word, i);
        resetSpanAt(Level::BigWord, i);
        if(state.previousLineWasEmpty && !curr.newLine()) {
          closeSpanAt(Level::Paragraph, i);
        }
      } else {
        if(state[Level::Word].hasCore &&
           VimCore::beginsWordBroad(prev, curr)) {
          closeSpanAt(Level::Word, i);
        }
        if(state[Level::BigWord].hasCore &&
           VimCore::beginsBigWordBroad(prev, curr)) {
          closeSpanAt(Level::BigWord, i);
        }
      }
    }

    addNode(Level::Char, TextRange{i, i + 1});

    // Adjust running flags
    if(curr.newLine()) {
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

  // Close any that are still open. These might be no-op, but that should be caught in closeSpanAt early return.
  closeSpanAt(Level::Word, sz);
  closeSpanAt(Level::BigWord, sz);
  closeSpanAt(Level::Line, sz);

  if (sz == 0 || text.back() == '\n') {
    addNode(
        Level::Line,
        TextRange{sz, sz},
        ChildRange{size(Level::BigWord), size(Level::BigWord)});
  }

  closeSpanAt(Level::Paragraph, sz);
  if (sz == 0) {
    addNode(
        Level::Paragraph,
        TextRange{0, 0},
        ChildRange{0, size(Level::Line)});
  }

  addNode(Level::Root, TextRange{0, sz}, ChildRange{0, size(Level::Root + 1)});
}

vector<DiffState> calculate(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options) {
  Tree initialTree(initialLines);
  Tree goalTree(goalLines);

  if (initialTree.text == goalTree.text) return {};

  Plan plan = Solver(initialTree, goalTree, config, options).solve();
  vector<DiffState> result;
  result.reserve(plan.spans.size());
  for (const EditSpan& span : plan.spans) {
    if (span.oldText.begin == span.oldText.end &&
        span.newText.begin == span.newText.end) {
      continue;
    }
    result.push_back(diffFromSpan(
        initialLines, initialTree, goalTree, span));
  }
  return result;
}

} // namespace TreeDiff
