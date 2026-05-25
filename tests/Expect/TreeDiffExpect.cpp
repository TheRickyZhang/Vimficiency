#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Optimizer/CompositionOptimizer/DiffState.h"
#include "Optimizer/CompositionOptimizer/TreeDiff.h"
#include "Keyboard/Config.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Utils/StringUtils.h"

using namespace std;

namespace {

struct SourceCase {
  string name;
  Lines initial;
  Lines goal;
};

class SourceGenerator {
public:
  explicit SourceGenerator(uint32_t seed) : state(seed) {}

  string ident() {
    constexpr string_view syllables[] = {
        "al", "be", "co", "di", "ex", "fn", "go", "hi",
    };
    string result = string(syllables[next(8)]);
    result += "_";
    result += string(syllables[next(8)]);
    return result;
  }

  int number() {
    return 1 + next(9);
  }

private:
  uint32_t state;

  int next(int modulo) {
    state = state * 1664525u + 1013904223u;
    return static_cast<int>((state >> 16) % static_cast<uint32_t>(modulo));
  }
};

filesystem::path fixtureDir() {
  return filesystem::path(__FILE__).parent_path() / "fixtures";
}

void expectReportFile(const filesystem::path& path, const string& actual) {
  if (getenv("VIMFY_UPDATE_TREE_DIFF_EXPECT") != nullptr) {
    ofstream out(path);
    ASSERT_TRUE(out) << "cannot write " << path;
    out << actual;
    return;
  }

  ifstream in(path);
  if (!in) {
    ADD_FAILURE() << "cannot open " << path << "\n\n" << actual;
    return;
  }

  ostringstream expected;
  expected << in.rdbuf();
  EXPECT_EQ(actual, expected.str());
}

string renderPos(const CursorPos& pos) {
  ostringstream out;
  out << "(" << pos.line << "," << pos.col << ")";
  return out.str();
}

const char* levelName(TreeDiff::Level level) {
  switch (level) {
    case TreeDiff::Level::Root:
      return "Root";
    case TreeDiff::Level::Paragraph:
      return "Paragraph";
    case TreeDiff::Level::Line:
      return "Line";
    case TreeDiff::Level::BigWord:
      return "BigWord";
    case TreeDiff::Level::Word:
      return "Word";
    case TreeDiff::Level::Char:
      return "Char";
  }
  return "Unknown";
}

string nodeText(const TreeDiff::Tree& tree, const TreeDiff::Node& node) {
  return makePrintable(string_view(tree.text).substr(
      static_cast<size_t>(node.text.begin),
      static_cast<size_t>(node.text.end - node.text.begin)));
}

void renderLines(ostream& out, string_view label, const Lines& lines) {
  out << label << ":\n";
  for (int i = 0; i < static_cast<int>(lines.size()); i++) {
    out << "  " << i << ": " << lines[i] << "\n";
  }
}

void renderTree(ostream& out, string_view label, const TreeDiff::Tree& tree) {
  constexpr TreeDiff::Level levels[] = {
      TreeDiff::Level::Root,
      TreeDiff::Level::Paragraph,
      TreeDiff::Level::Line,
      TreeDiff::Level::BigWord,
      TreeDiff::Level::Word,
  };

  out << label << " tree:\n";
  out << "  flat: \"" << makePrintable(tree.text) << "\"\n";
  out << "  counts:";
  for (int i = 0; i < TreeDiff::LEVEL_COUNT; i++) {
    const auto level = static_cast<TreeDiff::Level>(i);
    out << " " << levelName(level) << "=" << tree.size(level);
  }
  out << "\n";

  for (TreeDiff::Level level : levels) {
    out << "  " << levelName(level) << ":\n";
    const auto& nodes = tree[level];
    for (int i = 0; i < static_cast<int>(nodes.size()); i++) {
      const auto& node = nodes[static_cast<size_t>(i)];
      out << "    [" << i << "] text=[" << node.text.begin << ","
          << node.text.end << ") children=[" << node.children.begin << ","
          << node.children.end << ") \"" << nodeText(tree, node) << "\"\n";
    }
  }
}

void renderDiffs(ostream& out, const Lines& initial, const Lines& goal) {
  const vector<DiffState> diffs =
      TreeDiff::calculate(initial, goal, Config::uniform());

  out << "TreeDiff regions: " << diffs.size() << "\n";
  for (int i = 0; i < static_cast<int>(diffs.size()); i++) {
    const DiffState& diff = diffs[static_cast<size_t>(i)];
    const char* kind = diff.isPureInsertion() ? "insert"
                     : diff.isPureDeletion()  ? "delete"
                                             : "replace";
    const int beginFlat = DiffText::positionToFlatIndex(diff.beginPos, initial);
    const int endFlat = beginFlat + static_cast<int>(diff.deletedText.size());
    out << "  [" << i << "] " << kind
        << " range=" << renderPos(diff.beginPos) << "->"
        << renderPos(diff.endPos)
        << " flat=[" << beginFlat << "," << endFlat << ")\n";
    out << "      del=\"" << makePrintable(diff.deletedText) << "\"\n";
    out << "      ins=\"" << makePrintable(diff.insertedText) << "\"\n";
  }

  Lines reconstructed = Myers::applyAllDiffState(diffs, initial);
  const bool matchesGoal = reconstructed == goal;
  out << "Round trip: " << (matchesGoal ? "ok" : "mismatch") << "\n";
  if (!matchesGoal) {
    renderLines(out, "Reconstructed", reconstructed);
  }
}

Lines generatedSource(SourceGenerator& gen) {
  const string fn = gen.ident();
  const string value = gen.ident();
  const string left = gen.ident();
  const string right = gen.ident();

  return Lines{
      Line("int " + fn + "() {"),
      Line("  int " + value + " = " + left + " + " + right + ";"),
      Line("  " + value + " = " + value + " + " + left + ";"),
      Line("  return " + value + " + " + to_string(gen.number()) + ";"),
      Line("}"),
  };
}

SourceCase renameAndInsertCase() {
  SourceGenerator gen(17);
  Lines initial = generatedSource(gen);
  Lines goal = initial;
  goal[1].replace(goal[1].find(" + "), 3, " - ");
  goal[2] += " // adjusted";
  goal.insert(goal.begin() + 3, Line("  // generated guard"));

  return SourceCase{
      .name = "generated source arithmetic reshape",
      .initial = std::move(initial),
      .goal = std::move(goal),
  };
}

SourceCase blockReplacementCase() {
  SourceGenerator gen(83);
  Lines initial = generatedSource(gen);
  Lines goal = initial;
  goal[0].replace(0, 3, "long");
  goal[1] = "  long total = " + gen.ident() + " * " + to_string(gen.number()) + ";";
  goal[3] = "  return total;";

  return SourceCase{
      .name = "generated source block replacement",
      .initial = std::move(initial),
      .goal = std::move(goal),
  };
}

string renderReport() {
  const vector<SourceCase> cases{
      renameAndInsertCase(),
      blockReplacementCase(),
  };

  ostringstream out;
  out << "TreeDiff expect report\n";
  for (int i = 0; i < static_cast<int>(cases.size()); i++) {
    const SourceCase& test = cases[static_cast<size_t>(i)];
    if (i > 0) out << "\n";

    out << "Case: " << test.name << "\n";
    renderLines(out, "Initial", test.initial);
    renderLines(out, "Goal", test.goal);
    renderTree(out, "Initial", TreeDiff::Tree(test.initial));
    renderTree(out, "Goal", TreeDiff::Tree(test.goal));
    renderDiffs(out, test.initial, test.goal);
  }
  return out.str();
}

TEST(TreeDiffExpectTest, GeneratedSourceSamples) {
  expectReportFile(
      fixtureDir() / "tree_diff_generated_sources.expect.txt",
      renderReport());
}

}  // namespace
