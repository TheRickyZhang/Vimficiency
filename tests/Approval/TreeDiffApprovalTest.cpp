#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <iomanip>
#include <iterator>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "ApprovalTestUtils.h"
#include "Effort/RunningEffort.h"
#include "Optimizer/CompositionOptimizer/DiffState.h"
#include "Optimizer/CompositionOptimizer/Tree.h"
#include "Optimizer/CompositionOptimizer/TreeDiff.h"
#include "Keyboard/Config.h"
#include "Keyboard/KeyedSequence.h"
#include "Types/Lines.h"
#include "Utils/PrettyText.h"

using namespace std;
using namespace TreeDiff;

namespace {

constexpr int VISUAL_LEVEL_COUNT = LEVEL_COUNT - 1;
constexpr int LABEL_WIDTH = 4;

struct RenderedTreeRow {
  string text;
  vector<int> visualToByte;
  vector<int> charColumns;
};

struct RenderedTree {
  vector<RenderedTreeRow> rows;
};

struct DiffCost {
  double open = 0.0;
  double typed = 0.0;
  double total = 0.0;
};

string escapedText(string_view text) {
  string out;
  for (char c : text) {
    switch (c) {
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      default: out += c; break;
    }
  }
  return out;
}

vector<string> spacedGlyphCells(string_view text) {
  vector<string> cells;
  for (char c : text) {
    if (!cells.empty()) cells.push_back(" ");
    cells.push_back(VF::prettify(c));
  }
  return cells;
}

double typedTextEffort(string_view text, const Config& config) {
  KeyedSequence typed;
  typed.append(text);
  return RunningEffort(typed.keys, config).getEffort(config);
}

DiffCost costOf(const DiffState& diff,
                const Config& config,
                CostOptions options) {
  DiffCost cost{
      .open = options.diffOpenPenalty,
      .typed = typedTextEffort(diff.insertedText, config),
  };
  cost.total = cost.open + cost.typed;
  return cost;
}

vector<DiffCost> costsOf(const vector<DiffState>& diffs,
                         const Config& config,
                         CostOptions options) {
  vector<DiffCost> costs;
  costs.reserve(diffs.size());
  for (const DiffState& diff : diffs) {
    costs.push_back(costOf(diff, config, options));
  }
  return costs;
}

double totalCost(const vector<DiffCost>& costs) {
  double total = 0.0;
  for (const DiffCost& cost : costs) total += cost.total;
  return total;
}

string costLabel(DiffCost cost) {
  ostringstream out;
  out << cost.total << ": " << cost.open << " + " << cost.typed;
  return out.str();
}

RenderedTree renderTree(const Tree& tree) {
  const int n = ssize(tree.text);
  RenderedTree rendered;

  vector<string> glyphs;
  glyphs.reserve(n);
  for (char c : tree.text) {
    glyphs.push_back(VF::prettify(c));
  }
  if (n == 0) return rendered;

  for (int level = 0; level < VISUAL_LEVEL_COUNT; level++) {
    vector<bool> dividerAt(n + 1, false);
    for (const auto& node : tree[level]) {
      if (node.text.begin > 0) dividerAt[node.text.begin] = true;
      if (node.text.end < n) dividerAt[node.text.end] = true;
    }

    string row;
    int col = 0;
    vector<int> visualToByte{0};
    vector<int> charColumns(n, -1);
    auto append = [&](string_view text, int width) {
      row += text;
      for (int i = 0; i < width; i++) {
        visualToByte.push_back(ssize(row));
        col++;
      }
    };

    append(VF::PrettyText::TREE_OPEN, 1);
    for (int i = 0; i < n; i++) {
      if (i > 0) {
        append(
            dividerAt[i] ? VF::PrettyText::TREE_DIVIDER : string_view(" "),
            1);
      }
      charColumns[i] = col;
      append(glyphs[i], 1);
    }
    append(VF::PrettyText::TREE_CLOSE, 1);

    rendered.rows.push_back({
        .text = std::move(row),
        .visualToByte = std::move(visualToByte),
        .charColumns = std::move(charColumns),
    });
  }
  return rendered;
}

int annotationLevel(const Tree& tree, int begin, int end) {
  for (int level = VISUAL_LEVEL_COUNT - 1; level >= 0; level--) {
    for (const Tree::Node& node : tree[level]) {
      const bool contains = begin == end
          ? node.text.begin <= begin && begin <= node.text.end
          : node.text.begin <= begin && end <= node.text.end;
      if (contains) return level;
    }
  }
  return 0;
}

int columnForIndex(int index, const RenderedTreeRow& row) {
  if (index < ssize(row.charColumns)) {
    return row.charColumns[index];
  }
  return ssize(row.visualToByte) - 2;
}

int byteIndexForColumn(int col, const RenderedTreeRow& row) {
  if (col < 0) return 0;
  if (col >= ssize(row.visualToByte)) return ssize(row.text);
  return row.visualToByte[col];
}

int deletedWidth(int begin, int end, const RenderedTreeRow& row) {
  if (begin == end) return 0;
  return columnForIndex(end - 1, row) - columnForIndex(begin, row) + 1;
}

vector<string> overlayCells(
    const DiffState& diff,
    int begin,
    int end,
    int startCol,
    const RenderedTreeRow& row) {
  vector<string> cells = spacedGlyphCells(diff.insertedText);
  const int insertedWidth = ssize(cells);
  cells.resize(std::max(insertedWidth, deletedWidth(begin, end, row)), " ");

  for (int i = begin; i < end; i++) {
    const int offset = columnForIndex(i, row) - startCol;
    if (offset >= insertedWidth) {
      cells[offset] = VF::PrettyText::REMOVE;
    }
  }
  return cells;
}

void renderDiffTree(
    ostream& out,
    const Tree& tree,
    const Lines& initial,
    const vector<DiffState>& diffs,
    const vector<DiffCost>& costs) {
  struct Overlay {
    int col = 0;
    vector<string> cells;
    string cost;
  };
  struct BlankSpan {
    int col = 0;
    int width = 0;
  };
  array<vector<Overlay>, VISUAL_LEVEL_COUNT> overlays;
  array<vector<BlankSpan>, VISUAL_LEVEL_COUNT> blankSpans;
  const RenderedTree rendered = renderTree(tree);

  for (int i = 0; i < ssize(diffs); i++) {
    const DiffState& diff = diffs[i];
    const int begin = DiffText::positionToFlatIndex(diff.beginPos, initial);
    const int end = DiffText::positionToFlatIndex(diff.endPos, initial);
    const int level = annotationLevel(tree, begin, end);
    const auto& row = rendered.rows[level];
    const int startCol = columnForIndex(begin, row);
    const int oldWidth = deletedWidth(begin, end, row);
    vector<string> cells = overlayCells(diff, begin, end, startCol, row);
    const int newWidth = static_cast<int>(cells.size());
    if (newWidth > oldWidth) {
      blankSpans[level].push_back({
          .col = startCol + oldWidth,
          .width = newWidth - oldWidth,
      });
    }
    overlays[level].push_back({
        .col = startCol,
        .cells = std::move(cells),
        .cost = costLabel(costs[i]),
    });
  }

  for (int i = 0; i < ssize(rendered.rows); i++) {
    auto& rowBlankSpans = blankSpans[i];
    sort(rowBlankSpans.begin(), rowBlankSpans.end(),
         [](const BlankSpan& a, const BlankSpan& b) { return a.col < b.col; });

    auto& rowOverlays = overlays[i];
    sort(rowOverlays.begin(), rowOverlays.end(),
         [](const Overlay& a, const Overlay& b) { return a.col < b.col; });
    if (!rowOverlays.empty()) {
      string overlayLine;
      int visualCol = 0;
      auto adjustedCol = [&](int col) {
        int adjusted = col;
        for (const BlankSpan& span : rowBlankSpans) {
          if (span.col < col) adjusted += span.width;
        }
        return adjusted;
      };
      for (const Overlay& overlay : rowOverlays) {
        const int targetCol = LABEL_WIDTH + adjustedCol(overlay.col);
        if (visualCol < targetCol) {
          overlayLine.append(targetCol - visualCol, ' ');
          visualCol = targetCol;
        }
        for (const string& cell : overlay.cells) {
          overlayLine += cell;
          visualCol++;
        }
        overlayLine += "  " + overlay.cost;
        visualCol += 2 + static_cast<int>(overlay.cost.size());
      }
      out << overlayLine << "\n";
    }
    string rowText = rendered.rows[i].text;
    for (auto it = rowBlankSpans.rbegin(); it != rowBlankSpans.rend(); ++it) {
      rowText.insert(byteIndexForColumn(it->col, rendered.rows[i]), it->width, ' ');
    }
    out << left << setw(LABEL_WIDTH) << tree.size(i)
        << rowText << "\n";
  }
}

void writeTree(ostream& out, const Tree& tree) {
  const RenderedTree rendered = renderTree(tree);
  for (int i = 0; i < ssize(rendered.rows); i++) {
    out << left << setw(LABEL_WIDTH) << tree.size(i)
        << rendered.rows[i].text << "\n";
  }
}

string renderTreeDiff(
    string_view name,
    string_view initialText,
    string_view goalText) {
  const Lines initial = Lines::unflatten(initialText);
  const Lines goal = Lines::unflatten(goalText);
  const Config config = Config::uniform();
  const CostOptions costOptions;
  const vector<DiffState> diffs =
      calculate(initial, goal, config, costOptions);
  const vector<DiffCost> costs = costsOf(diffs, config, costOptions);
  const Tree initialTree(initial);
  const Tree goalTree(goal);

  ostringstream out;
  out << name << "\n";
  out << escapedText(initialText) << "\n";
  out << escapedText(goalText) << "\n";
  out << "\n";
  writeTree(out, initialTree);
  out << "\n";
  writeTree(out, goalTree);
  out << "\n";
  out << "cost " << totalCost(costs) << "\n";
  renderDiffTree(out, initialTree, initial, diffs, costs);
  return out.str();
}

TEST(TreeDiffApproval, BlankLineParagraphBoundary) {
  verifyText(renderTreeDiff(
      "test 1",
      "a\nbc\n\nd",
      "a\nxy\n\nd"));
}

TEST(TreeDiffApproval, SpacedWordsAcrossParagraphs) {
  verifyText(renderTreeDiff(
      "test 2",
      "a. b\n\nc  c\n",
      "abbb\n\n\ncccc"));
}

TEST(TreeDiffApproval, TinyCodeBlock) {
  verifyText(renderTreeDiff(
      "code block",
      "int main {\n"
      "  int x = 0;\n"
      "  x++;\n"
      "}",
      "int32_t main {\n"
      "  int y = 2;\n"
      "  y++; }"));
}

}  // namespace
