#include "TreeDiff.h"

#include <cassert>
#include <iterator>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Effort/RunningEffort.h"
#include "Keyboard/KeyedSequence.h"
#include "Tree.h"
#include "Utils/PrettyText.h"

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

using TextRange = Tree::TextRange;
using ChildRange = Tree::ChildRange;
using Node = Tree::Node;

Level childLevel(Level level) {
  assert(level != Level::Char);
  return level + 1;
}

struct EditSpan {
  TextRange oldText;
  TextRange newText;

  bool empty() const { return oldText.empty() && newText.empty(); }
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
  return string_view(oldTree.text).substr(oldRange.begin, textSize(oldRange)) ==
         string_view(newTree.text).substr(newRange.begin, textSize(newRange));
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
      span.oldText.begin,
      textSize(span.oldText));
  string insertedText = goalTree.text.substr(
      span.newText.begin,
      textSize(span.newText));

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
    const Node oldParent = topParent(oldTree);
    const Node newParent = topParent(newTree);
    return solveChildren(Level::Root, oldParent, newParent);
  }

private:
  static constexpr double INF = numeric_limits<double>::max() / 4.0;

  const Tree& oldTree;
  const Tree& newTree;
  const Config& config;
  CostOptions options;

  static Node topParent(const Tree& tree) {
    if (!tree[Level::Root].empty()) return tree[Level::Root].front();
    return Node{
        .text = TextRange{0, 0},
        .children = ChildRange{0, 0},
    };
  }

  double typedTextEffort(string_view text) const {
    KeyedSequence typed;
    typed.append(text);
    RunningEffort effort(typed.keys, config);
    return effort.getEffort(config);
  }

  double typedRangeEffort(const Tree& tree, TextRange range) const {
    return typedTextEffort(string_view(tree.text).substr(
        range.begin,
        range.end - range.begin));
  }

  double diffSpanCost(const EditSpan& span) const {
    RunningEffort effort;
    effort.addPenalty(options.diffOpenPenalty);
    KeyedSequence typed;
    typed.append(string_view(newTree.text).substr(
        span.newText.begin,
        textSize(span.newText)));
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
              oldCount + 1,
              vector<double>(newCount + 1, INF)),
          innerCost(
              oldCount + 1,
              vector<double>(newCount + 1, INF)),
          outerChoice(
              oldCount + 1,
              vector<OuterChoice>(newCount + 1, OuterChoice::Unset)),
          innerChoice(
              oldCount + 1,
              vector<InnerBuild>(newCount + 1)),
          newRangeEffort(
              newCount + 1,
              vector<double>(newCount + 1, 0.0)) {
      for (int begin = 0; begin <= newCount; begin++) {
        for (int end = begin + 1; end <= newCount; end++) {
          newRangeEffort[begin][end] =
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

      double& res = outerCost[i][j];
      if (res != INF) return res;

      auto update = [&](double cost, OuterChoice choice) {
        if (cost < res) {
          res = cost;
          outerChoice[i][j] = choice;
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

      double& res = innerCost[i][j];
      if (res != INF) return res;

      auto update = [&](double cost, int nextI, int nextJ) {
        if (cost < res) {
          res = cost;
          innerChoice[i][j] = InnerBuild{.nextI = nextI, .nextJ = nextJ};
        }
      };

      for (int nextI = i; nextI <= oldCount; nextI++) {
        for (int nextJ = j; nextJ <= newCount; nextJ++) {
          if (nextI == i && nextJ == j) continue;
          double insertedCost = newRangeEffort[j][nextJ];
          update(insertedCost + outer(nextI, nextJ), nextI, nextJ);
        }
      }

      return res;
    }

    vector<EditSpan> buildOuter(int i, int j) {
      if (i == oldCount && j == newCount) return {};

      outer(i, j);
      switch (outerChoice[i][j]) {
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
      InnerBuild build = innerChoice[i][j];
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

void formatDiffs(std::ostream& out,
                 const vector<DiffState>& diffs,
                 const Lines& initialLines) {
  out << "TreeDiff regions: " << diffs.size() << "\n";
  for (int i = 0; i < ssize(diffs); i++) {
    const DiffState& diff = diffs[i];
    const char* kind = diff.isPureInsertion() ? "insert"
                     : diff.isPureDeletion()  ? "delete"
                                              : "replace";
    const int beginFlat =
        DiffText::positionToFlatIndex(diff.beginPos, initialLines);
    const int endFlat = beginFlat + ssize(diff.deletedText);
    out << "  [" << i << "] " << kind
        << " (" << diff.beginPos.line << "," << diff.beginPos.col
        << ")->(" << diff.endPos.line << "," << diff.endPos.col << ")"
        << " flat=[" << beginFlat << "," << endFlat << ") ";
    if (diff.isPureInsertion()) {
      out << "\"" << VF::PrettyText(diff.insertedText) << "\"";
    } else if (diff.isPureDeletion()) {
      out << "\"" << VF::PrettyText(diff.deletedText) << "\"";
    } else {
      out << "\"" << VF::PrettyText(diff.deletedText)
          << "\" " << VF::PrettyText::ARROW << " \""
          << VF::PrettyText(diff.insertedText) << "\"";
    }
    out << "\n";
  }
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
    if (span.empty()) continue;
    result.push_back(diffFromSpan(
        initialLines, initialTree, goalTree, span));
  }
  return result;
}

} // namespace TreeDiff
