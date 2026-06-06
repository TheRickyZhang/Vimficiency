#include "TreeDiff.h"

#include <cassert>
#include <iterator>
#include <limits>
#include <map>
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

// Keystroke cost of the bare motion to traverse one unit at a given level:
// l/w/j = 1, W/} (shifted) = 2. Movement sums this per kept child between edits.
double levelCost(Level level) {
  switch (level) {
    case Level::Paragraph: return 2.0;  // }
    case Level::Line:      return 1.0;  // j
    case Level::BigWord:   return 2.0;  // W
    case Level::Word:      return 1.0;  // w
    case Level::Char:      return 1.0;  // l
    default:               return 0.0;  // Root: whole buffer, never a diff unit
  }
}

// Keystroke cost of deleting a contiguous run at a given level: the motion plus
// the `d` operator, count-independent (one command). char is `x` (a single key,
// no operator). Pricing a counted delete as one command is what keeps a
// fine-level multi-delete (4dd) from being dominated by a coarse one (dap).
double deleteCost(Level level) {
  return level == Level::Char ? 1.0 : levelCost(level) + 1.0;
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

  RunningEffort rangeEffort(const Tree& tree, TextRange range) const {
    KeyedSequence typed;
    typed.append(string_view(tree.text).substr(
        range.begin,
        range.end - range.begin));
    return RunningEffort(typed.keys, config);
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
    double unitCost_;    // movement keystrokes per kept child at childLevel_
    double deleteUnit_;  // deletion keystrokes per delete region at childLevel_
    int prefixMatchLen_ = 0;  // leading run of identical children (free, pre-first-edit)
    int suffixMatchLen_ = 0;  // trailing run of identical children (free tail)
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
    // Memoized suffix-min over old-end r: min_{r>=i} outer(r, c).
    vector<vector<double>> bestRCost;
    vector<vector<int>> bestRArg;

    ListDp(Solver& solver,
           Level parentLevel,
           const Node& oldParent,
           const Node& newParent)
        : solver(solver),
          childLevel_(childLevel(parentLevel)),
          unitCost_(levelCost(childLevel_) * solver.options.moveDeleteScale),
          deleteUnit_(deleteCost(childLevel_) * solver.options.moveDeleteScale),
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
              vector<double>(newCount + 1, 0.0)),
          bestRCost(
              oldCount + 1,
              vector<double>(newCount + 1, INF)),
          bestRArg(
              oldCount + 1,
              vector<int>(newCount + 1, -1)) {
      while (prefixMatchLen_ < oldCount && prefixMatchLen_ < newCount &&
             sameText(solver.oldTree, oldChild(prefixMatchLen_),
                      solver.newTree, newChild(prefixMatchLen_)))
        prefixMatchLen_++;
      while (suffixMatchLen_ < oldCount && suffixMatchLen_ < newCount &&
             sameText(solver.oldTree, oldChild(oldCount - 1 - suffixMatchLen_),
                      solver.newTree, newChild(newCount - 1 - suffixMatchLen_)))
        suffixMatchLen_++;
      // Effort over each child segment is computed once, then ranges are built
      // by O(1) monoid merges instead of re-walking every [begin,end) substring.
      vector<RunningEffort> segEffort;
      segEffort.reserve(newCount);
      for (int k = 0; k < newCount; k++) {
        segEffort.push_back(solver.rangeEffort(
            solver.newTree, TextRange{boundaryNew(k), boundaryNew(k + 1)}));
      }
      for (int begin = 0; begin < newCount; begin++) {
        RunningEffort acc = segEffort[begin];
        newRangeEffort[begin][begin + 1] = acc.getEffort(solver.config);
        for (int end = begin + 2; end <= newCount; end++) {
          newRangeEffort[begin][end] =
              acc.appendFrom(segEffort[end - 1], solver.config);
        }
      }
    }

    Plan solve() {
      return Plan{
          .cost = outer(0, 0),
          .spans = buildOuter(0, 0),
      };
    }

    // Remaining children are an identical run to the end: kept for free, no
    // movement charged (there is no edit after them to traverse toward).
    bool tailFree(int i, int j) const {
      return oldCount - i == newCount - j && oldCount - i <= suffixMatchLen_;
    }

    // Matched children before the first edit: kept for free. Movement is only
    // charged for traversal *between* edits; the run to the first edit is the
    // start bumper, which is partition-neutral and excluded.
    bool leadingFree(int i, int j) const {
      return i == j && i < prefixMatchLen_;
    }

    double outer(int i, int j) {
      if (i == oldCount && j == newCount) return 0;
      if (tailFree(i, j)) return 0;

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
          // Traversing a kept child costs movement, except on the leading run.
          double move = leadingFree(i, j) ? 0.0 : unitCost_;
          update(outer(i + 1, j + 1) + move, OuterChoice::Keep);
        } else if (childLevel_ != Level::Char &&
                   sameTextOutsideChildren(
                       solver.oldTree, oldNode,
                       solver.newTree, newNode,
                       childLevel_)) {
          const Plan& child = solver.solveChildren(childLevel_, oldNode, newNode);
          update(child.cost + outer(i + 1, j + 1), OuterChoice::Recurse);
        }
      }

      if (i < oldCount || j < newCount) {
        update(solver.options.diffOpenPenalty + inner(i, j), OuterChoice::Diff);
      }

      return res;
    }

    // Suffix-min over the old-end r in [i, oldCount] of outer(r, c), ties to the
    // smallest r. Deletion is a flat per-region command cost, so it is not
    // folded here — this just finds the cheapest place to resume after deleting.
    double bestOldEnd(int i, int c, int& argR) {
      double& memo = bestRCost[i][c];
      if (memo != INF) {
        argR = bestRArg[i][c];
        return memo;
      }
      double here = outer(i, c);
      if (i == oldCount) {
        bestRArg[i][c] = oldCount;
        argR = oldCount;
        return memo = here;
      }
      int restArg;
      double rest = bestOldEnd(i + 1, c, restArg);
      if (here <= rest) {
        bestRArg[i][c] = i;
        argR = i;
        return memo = here;
      }
      bestRArg[i][c] = restArg;
      argR = restArg;
      return memo = rest;
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

      // A diff region deletes old [i, nextI) and inserts new [j, nextJ). Two
      // shapes per end column nextJ:
      //  - pure insert (nextI == i): no delete cost; valid only when nextJ > j.
      //  - delete + insert (nextI > i): one delete command (deleteUnit_, flat),
      //    plus the cheapest resume point via the outer suffix-min.
      for (int nextJ = j; nextJ <= newCount; nextJ++) {
        if (nextJ > j) {
          update(newRangeEffort[j][nextJ] + outer(i, nextJ), i, nextJ);
        }
        if (i < oldCount) {
          int argR;
          double endCost = bestOldEnd(i + 1, nextJ, argR);
          update(newRangeEffort[j][nextJ] + deleteUnit_ + endCost, argR, nextJ);
        }
      }

      return res;
    }

    vector<EditSpan> buildOuter(int i, int j) {
      if (i == oldCount && j == newCount) return {};
      if (tailFree(i, j)) return {};

      outer(i, j);
      switch (outerChoice[i][j]) {
        case OuterChoice::Keep:
          return buildOuter(i + 1, j + 1);

        case OuterChoice::Recurse: {
          const Plan& child = solver.solveChildren(
              childLevel_, oldChild(i), newChild(j));
          vector<EditSpan> result = child.spans;
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

      const Plan& refined = solver.solveChildren(
          childLevel_,
          Node{.text = span.oldText,
               .children = childRangeForOld(startI, endI)},
          Node{.text = span.newText,
               .children = childRangeForNew(startJ, endJ)});

      // Coarse = this span as one diff: penalty + inserted effort + deleting the
      // whole old span at this level. Refined prices the same span split a level
      // down (with its own finer deletions/movement), so both must charge the
      // deletion for the comparison to be fair.
      double coarseCost =
          solver.diffSpanCost(span) + (endI - startI) * unitCost_;
      if (!refined.spans.empty() && refined.cost <= coarseCost) {
        return refined.spans;
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

  // A ListDp is fully determined by its child level, the old/new child ranges,
  // and the parent text starts (the latter only matter for empty child ranges).
  // Memoizing on this key collapses the cost-pass / build-pass / refine
  // re-solves that otherwise recompute the same subtree many times over.
  struct PlanKey {
    int childLevel;
    int oldBegin;
    int oldEnd;
    int newBegin;
    int newEnd;
    int oldTextBegin;
    int newTextBegin;
    auto operator<=>(const PlanKey&) const = default;
  };

  std::map<PlanKey, Plan> planCache;

  const Plan& solveChildren(Level parentLevel,
                            const Node& oldParent,
                            const Node& newParent) {
    PlanKey key{
        levelIndex(childLevel(parentLevel)),
        oldParent.children.begin,
        oldParent.children.end,
        newParent.children.begin,
        newParent.children.end,
        oldParent.text.begin,
        newParent.text.begin,
    };
    auto it = planCache.find(key);
    if (it != planCache.end()) return it->second;

    ListDp dp(*this, parentLevel, oldParent, newParent);
    Plan plan = dp.solve();
    return planCache.emplace(key, std::move(plan)).first->second;
  }
};

// A contiguous edit that straddles a token boundary is emitted as two adjacent
// regions across the recursion seam (e.g. inserting " " then "world"). Those are
// a representational artifact, not a partition decision — merge regions that abut
// in both old and new coordinates back into one. See dev/optimizer/diff-generation.md.
vector<EditSpan> mergeAdjacentSpans(vector<EditSpan> spans) {
  vector<EditSpan> merged;
  for (const EditSpan& span : spans) {
    if (span.empty()) continue;
    if (!merged.empty()) {
      EditSpan& prev = merged.back();
      if (prev.oldText.end == span.oldText.begin &&
          prev.newText.end == span.newText.begin) {
        prev.oldText.end = span.oldText.end;
        prev.newText.end = span.newText.end;
        continue;
      }
    }
    merged.push_back(span);
  }
  return merged;
}

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

  vector<EditSpan> spans = mergeAdjacentSpans(std::move(plan.spans));
  vector<DiffState> result;
  result.reserve(spans.size());
  for (const EditSpan& span : spans) {
    result.push_back(diffFromSpan(
        initialLines, initialTree, goalTree, span));
  }
  return result;
}

} // namespace TreeDiff
