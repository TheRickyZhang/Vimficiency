#include <gtest/gtest.h>

#include <random>
#include <string>
#include <vector>

#include "Effort/RunningEffort.h"
#include "Keyboard/Config.h"
#include "Optimizer/DiffPlanner/VimDiff.h"
#include "Optimizer/DiffPlanner/DiffState.h"
#include "Optimizer/DiffPlanner/MyersDiff.h"
#include "Types/Lines.h"

using namespace std;

namespace {

Lines toLines(const string& flat) { return Lines::unflatten(flat); }

vector<DiffState> bestDiffs(const Lines& initial, const Lines& goal, const Config& config) {
  return VimDiff::calculate(initial, goal, config).diffs;
}

// Applying VimDiff's diffs in sequence must reproduce the goal. This validates
// the DP reconstruction + DiffState production end-to-end, independent of the
// cost oracle.
void expectRoundTrip(const string& initial, const string& goal) {
  Config config = Config::uniform();
  Lines initLines = toLines(initial);
  Lines goalLines = toLines(goal);
  vector<DiffState> diffs = bestDiffs(initLines, goalLines, config);
  Lines applied = MyersDiff::applyAllDiffState(diffs, initLines);
  EXPECT_EQ(applied.flatten(), goalLines.flatten())
      << "round-trip failed for \"" << initial << "\" -> \"" << goal << "\"";
}

// Over the DP cell budget the planner falls back to the sealed partition: one
// region per unmatched block, still reproducing the goal. A 200-char word
// seals (one `W` vs 200 keystrokes) with margins bounded by half the run.
TEST(VimDiffTest, OverCellBudgetFallsBackToSealedPartition) {
  Config config = Config::uniform();
  const string run(200, 'x');
  Lines initial = toLines("AAAAAAAAAA" + run + "BBBBBBBBBB");
  Lines goal = toLines("CCCCCCCCCC" + run + "DDDDDDDDDD");
  const int mid = 10 + (int)run.size() / 2;

  VimDiff::Plan plan = VimDiff::calculate(
      initial, goal, config, VimDiff::CostOptions{.maxPlannerCells = 16});
  const vector<DiffState>& diffs = plan.diffs;
  ASSERT_EQ(diffs.size(), 2u);
  EXPECT_EQ(diffs[0].beginPos, CursorPos(0, 0));
  EXPECT_LE(diffs[0].endPos.col, mid);
  EXPECT_GE(diffs[1].beginPos.col, mid);
  EXPECT_EQ(diffs[1].endPos.col, (int)initial.flatten().size());
  EXPECT_EQ(MyersDiff::applyAllDiffState(diffs, initial).flatten(), goal.flatten());
  EXPECT_GT(plan.cost, 0.0);
}

TEST(VimDiffTest, RoundTripsHandcrafted) {
  expectRoundTrip("abc", "abc");
  expectRoundTrip("abc", "abXc");
  expectRoundTrip("hello world", "hello");
  expectRoundTrip("foo", "bar");
  expectRoundTrip("abcdef", "abXYef");
  expectRoundTrip("a b c", "a c");
  expectRoundTrip("x + x", "y + y");
  expectRoundTrip("ab cd ef", "ab XX ef");
  expectRoundTrip("aaa\nbbb", "aaa bbb");
  expectRoundTrip("", "hello");
  expectRoundTrip("hello", "");
  expectRoundTrip("foo.bar", "baz.qux");
  expectRoundTrip("one two three four five", "one TWO three FOUR five");
  expectRoundTrip("alpha\nbeta\ngamma\ndelta", "alpha\nBETA\ngamma\nDELTA");
}

// A long unchanged block between two edits must be kept (collapsed to a single
// reduced unit), never retyped: the unified planner returns exactly the two
// surgical edits, round-trips, and leaves the whole middle untouched. The rich
// multi-line content exercises the collapse gate and the reduced-coordinate DP.
TEST(VimDiffTest, KeepsLongMiddleBlockAsTwoRegions) {
  Config config = Config::uniform();
  const string keep =
      "alpha beta gamma\ndelta epsilon zeta\neta theta iota\nkappa lambda mu\n"
      "nu xi omicron\npi rho sigma\ntau upsilon phi\nchi psi omega\n";
  Lines initial = toLines("first XX line\n" + keep + "last YY line");
  Lines goal = toLines("first ZZZ line\n" + keep + "last WW line");

  const vector<DiffState> diffs = bestDiffs(initial, goal, config);
  EXPECT_EQ(MyersDiff::applyAllDiffState(diffs, initial).flatten(), goal.flatten());
  ASSERT_EQ(diffs.size(), 2u);
  EXPECT_EQ(diffs[0].deletedText, "XX");
  EXPECT_EQ(diffs[0].insertedText, "ZZZ");
  EXPECT_EQ(diffs[1].deletedText, "YY");
  EXPECT_EQ(diffs[1].insertedText, "WW");
}

// Randomized separated edits (a first-line prefix insert and a last-line suffix
// append around many unchanged middle lines): the planner round-trips and keeps
// the middle as exactly two surgical regions.
TEST(VimDiffTest, SeparatedEditsStayTwoRegions) {
  Config config = Config::uniform();
  mt19937 rng(20260612);
  uniform_int_distribution<int> wordLen(2, 7);
  uniform_int_distribution<int> letter(0, 25);
  auto randWord = [&] {
    string w;
    for (int i = wordLen(rng); i > 0; i--) w += (char)('a' + letter(rng));
    return w;
  };

  for (int it = 0; it < 6; it++) {
    vector<string> lines;
    for (int l = 0; l < 12; l++) {
      string line;
      for (int w = 0; w < 4; w++) line += (w ? " " : "") + randWord();
      lines.push_back(line);
    }
    Lines initial(lines.begin(), lines.end());
    vector<string> goalVec = lines;
    goalVec.front() = randWord() + " " + goalVec.front();
    goalVec.back() += " " + randWord();
    Lines goal(goalVec.begin(), goalVec.end());

    const vector<DiffState> diffs = bestDiffs(initial, goal, config);
    EXPECT_EQ(MyersDiff::applyAllDiffState(diffs, initial).flatten(), goal.flatten())
        << "case " << it;
    EXPECT_EQ(diffs.size(), 2u) << "case " << it;
  }
}

TEST(VimDiffTest, IdenticalBuffersProduceNoDiffs) {
  Config config = Config::uniform();
  Lines lines = toLines("hello world\nsecond line");
  EXPECT_TRUE(bestDiffs(lines, lines, config).empty());
}

TEST(VimDiffTest, TrailingDeletionIsSingleRegion) {
  Config config = Config::uniform();
  vector<DiffState> diffs =
      bestDiffs(toLines("hello world"), toLines("hello"), config);
  ASSERT_EQ(diffs.size(), 1u);
  EXPECT_TRUE(diffs[0].isPureDeletion());
  EXPECT_EQ(diffs[0].deletedText, " world");
}

TEST(VimDiffTest, InteriorInsertionIsSingleRegion) {
  Config config = Config::uniform();
  vector<DiffState> diffs =
      bestDiffs(toLines("abc"), toLines("abXc"), config);
  ASSERT_EQ(diffs.size(), 1u);
  EXPECT_TRUE(diffs[0].isPureInsertion());
  EXPECT_EQ(diffs[0].insertedText, "X");
}

TEST(VimDiffTest, RoundTripsRandomSmall) {
  mt19937 rng(98765);
  const string alphabet = "ab c\n";
  int tested = 0, failures = 0;
  auto gen = [&]() {
    int len = 1 + (int)(rng() % 7);
    string s;
    for (int k = 0; k < len; k++) s += alphabet[rng() % alphabet.size()];
    while (!s.empty() && s.front() == '\n') s.front() = 'a';
    while (!s.empty() && s.back() == '\n') s.back() = 'b';
    return s;
  };
  Config config = Config::uniform();
  for (int it = 0; it < 3000; it++) {
    string o = gen(), g = gen();
    Lines ol = toLines(o), gl = toLines(g);
    vector<DiffState> diffs = bestDiffs(ol, gl, config);
    if (MyersDiff::applyAllDiffState(diffs, ol).flatten() != gl.flatten()) {
      failures++;
      if (failures <= 5)
        ADD_FAILURE() << "round-trip failed: \"" << o << "\" -> \"" << g << "\"";
    }
    tested++;
  }
  EXPECT_EQ(failures, 0) << failures << "/" << tested << " random round-trips failed";
}

// The breakdown re-prices the plan region-by-region with single-span oracle
// queries; its total must equal the DP's path cost. This pins the region walk,
// the block-local sweeps, and the CROSS moves to what the path paid for.
TEST(VimDiffTest, BreakdownTotalMatchesPlanCost) {
  Config config = Config::qwerty();
  mt19937 rng(4242);
  uniform_int_distribution<int> ch(0, 4), lineCount(1, 5), lineLen(0, 14);
  for (int it = 0; it < 300; it++) {
    vector<string> v;
    for (int l = lineCount(rng); l > 0; l--) {
      string s;
      for (int k = lineLen(rng); k > 0; k--) s += char('a' + ch(rng));
      v.push_back(s);
    }
    Lines initial(v.begin(), v.end());
    string g = initial.flatten();
    for (int k = 1 + (int)(rng() % 4); k > 0; k--) {
      int op = (int)(rng() % 3);
      int pos = (int)(rng() % (g.size() + 1));
      if (op == 0 && pos < (int)g.size()) g[pos] = char('a' + ch(rng));
      else if (op == 1) g.insert(g.begin() + pos, char('a' + ch(rng)));
      else if (pos < (int)g.size()) g.erase(g.begin() + pos);
    }
    Lines goal = Lines::unflatten(g);
    VimDiff::Plan plan = VimDiff::calculate(initial, goal, config);
    VimDiff::CostBreakdown bd = VimDiff::calculateBreakdown(initial, goal, config);
    EXPECT_NEAR(plan.cost, bd.total, 1e-9) << "\"" << initial.flatten() << "\" -> \"" << g << "\"";
    ASSERT_EQ(plan.diffs.size(), bd.regions.size());
    for (size_t r = 0; r < plan.diffs.size(); r++) {
      EXPECT_EQ(plan.diffs[r].deletedText, bd.regions[r].diff.deletedText);
      EXPECT_EQ(plan.diffs[r].insertedText, bd.regions[r].diff.insertedText);
    }
  }
}

// A replace region's insert phase skips the entry key — the deletion's change
// form enters insert mode itself. A pure insertion still pays it.
TEST(VimDiffTest, ChangeEdgeWaivesEntryKeyAfterDelete) {
  Config config = Config::qwerty();
  const double entry = getEffort("i", config), esc = getEffort("<Esc>", config);

  VimDiff::CostBreakdown replace =
      VimDiff::calculateBreakdown(toLines("hello"), toLines("jello"), config);
  ASSERT_EQ(replace.regions.size(), 1u);
  EXPECT_EQ(replace.regions[0].diff.deletedText, "h");
  EXPECT_DOUBLE_EQ(replace.regions[0].ins, esc + getEffort("j", config));

  VimDiff::CostBreakdown insertion =
      VimDiff::calculateBreakdown(toLines("abc"), toLines("abXc"), config);
  ASSERT_EQ(insertion.regions.size(), 1u);
  EXPECT_TRUE(insertion.regions[0].diff.deletedText.empty());
  EXPECT_DOUBLE_EQ(insertion.regions[0].ins, entry + esc + getEffort("X", config));
}

}  // namespace
