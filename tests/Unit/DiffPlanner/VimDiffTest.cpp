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

// Best partition's diffs (empty when initial == goal).
vector<DiffState> bestDiffs(const Lines& initial, const Lines& goal, const Config& config) {
  vector<VimDiff::Plan> plans = VimDiff::calculate(initial, goal, config);
  return plans.empty() ? vector<DiffState>{} : plans.front().diffs;
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

  vector<VimDiff::Plan> plans = VimDiff::calculate(
      initial, goal, config, VimDiff::CostOptions{.maxPlannerCells = 16});
  ASSERT_EQ(plans.size(), 1u);
  const vector<DiffState>& diffs = plans.front().diffs;
  ASSERT_EQ(diffs.size(), 2u);
  EXPECT_EQ(diffs[0].beginPos, CursorPos(0, 0));
  EXPECT_LE(diffs[0].endPos.col, mid);
  EXPECT_GE(diffs[1].beginPos.col, mid);
  EXPECT_EQ(diffs[1].endPos.col, (int)initial.flatten().size());
  EXPECT_EQ(MyersDiff::applyAllDiffState(diffs, initial).flatten(), goal.flatten());
  EXPECT_GT(plans.front().cost, 0.0);
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

  vector<VimDiff::Plan> plans = VimDiff::calculate(initial, goal, config);
  ASSERT_FALSE(plans.empty());
  EXPECT_EQ(MyersDiff::applyAllDiffState(plans.front().diffs, initial).flatten(),
            goal.flatten());

  const vector<DiffState>& diffs = plans.front().diffs;
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

    vector<VimDiff::Plan> plans = VimDiff::calculate(initial, goal, config);
    ASSERT_FALSE(plans.empty());
    EXPECT_EQ(MyersDiff::applyAllDiffState(plans.front().diffs, initial).flatten(),
              goal.flatten())
        << "case " << it;
    EXPECT_EQ(plans.front().diffs.size(), 2u) << "case " << it;
  }
}

TEST(VimDiffTest, IdenticalBuffersProduceNoDiffs) {
  Config config = Config::uniform();
  Lines lines = toLines("hello world\nsecond line");
  EXPECT_TRUE(VimDiff::calculate(lines, lines, config).empty());
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

// Every plan the K-best DP returns must be a valid partition (reconstructs the
// goal), the list must be ascending by cost, and the plans must be distinct —
// the invariants that make top-K meaningful, exercised on the rank>0 backpointer
// walks that the single-plan path never touches.
TEST(VimDiffTest, AllPlansRoundTripAscendAndDiffer) {
  Config config = Config::uniform();
  const vector<pair<string, string>> cases = {
      {"aaa bbb ccc", "xxx bbb yyy"}, {"abc xyz", "aXc xYz"},
      {"ab cd ef", "Xb cd eY"},       {"the quick fox", "the slow fox"},
      {"hello world", "hello"},       {"foo", "bar"},
      {"aaa\nbbb", "aaa bbb"},        {"x + x", "y + y"},
  };
  VimDiff::CostOptions options;
  options.maxPlans = 6;
  for (const auto& [init, goal] : cases) {
    Lines ol = toLines(init), gl = toLines(goal);
    vector<VimDiff::Plan> plans = VimDiff::calculate(ol, gl, config, options);
    ASSERT_FALSE(plans.empty()) << init << " -> " << goal;

    double prev = -1.0;
    set<string> signatures;
    for (size_t p = 0; p < plans.size(); p++) {
      EXPECT_GE(plans[p].cost, prev - 1e-9)
          << "costs not ascending at plan " << p << " for \"" << init << "\"";
      prev = plans[p].cost;

      Lines applied = MyersDiff::applyAllDiffState(plans[p].diffs, ol);
      EXPECT_EQ(applied.flatten(), gl.flatten())
          << "plan " << p << " round-trip failed for \"" << init << "\" -> \"" << goal << "\"";

      string sig;
      for (const DiffState& d : plans[p].diffs)
        sig += to_string(d.beginPos.line) + ":" + to_string(d.beginPos.col) + ":" +
               d.deletedText + ">" + d.insertedText + "|";
      signatures.insert(sig);
    }
    EXPECT_EQ(signatures.size(), plans.size())
        << "duplicate plans for \"" << init << "\" -> \"" << goal << "\"";
  }
}

// The breakdown re-prices each plan region-by-region from the transition
// tables; its total must equal the DP's path cost for the same plan. This pins
// the region walk and the transition tables to the DP path cost.
TEST(VimDiffTest, BreakdownTotalsMatchPlanCosts) {
  Config config = Config::qwerty();
  mt19937 rng(4242);
  uniform_int_distribution<int> ch(0, 4), lineCount(1, 5), lineLen(0, 14);
  VimDiff::CostOptions options;
  options.maxPlans = 4;
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
    vector<VimDiff::Plan> plans = VimDiff::calculate(initial, goal, config, options);
    vector<VimDiff::CostBreakdown> bds = VimDiff::calculateBreakdown(initial, goal, config, options);
    ASSERT_EQ(plans.size(), bds.size()) << initial.flatten() << " -> " << g;
    for (size_t p = 0; p < plans.size(); p++) {
      EXPECT_NEAR(plans[p].cost, bds[p].total, 1e-9)
          << "plan " << p << " for \"" << initial.flatten() << "\" -> \"" << g << "\"";
      ASSERT_EQ(plans[p].diffs.size(), bds[p].regions.size());
      for (size_t r = 0; r < plans[p].diffs.size(); r++) {
        EXPECT_EQ(plans[p].diffs[r].deletedText, bds[p].regions[r].diff.deletedText);
        EXPECT_EQ(plans[p].diffs[r].insertedText, bds[p].regions[r].diff.insertedText);
      }
    }
  }
}

// A replace region's insert phase skips the entry key — the deletion's change
// form enters insert mode itself. A pure insertion still pays it.
TEST(VimDiffTest, ChangeEdgeWaivesEntryKeyAfterDelete) {
  Config config = Config::qwerty();
  const double entry = getEffort("i", config), esc = getEffort("<Esc>", config);

  vector<VimDiff::CostBreakdown> replace =
      VimDiff::calculateBreakdown(toLines("hello"), toLines("jello"), config);
  ASSERT_EQ(replace.front().regions.size(), 1u);
  EXPECT_EQ(replace.front().regions[0].diff.deletedText, "h");
  EXPECT_DOUBLE_EQ(replace.front().regions[0].ins, esc + getEffort("j", config));

  vector<VimDiff::CostBreakdown> insertion =
      VimDiff::calculateBreakdown(toLines("abc"), toLines("abXc"), config);
  ASSERT_EQ(insertion.front().regions.size(), 1u);
  EXPECT_TRUE(insertion.front().regions[0].diff.deletedText.empty());
  EXPECT_DOUBLE_EQ(insertion.front().regions[0].ins, entry + esc + getEffort("X", config));
}

}  // namespace
