#include <gtest/gtest.h>

#include <random>
#include <string>
#include <vector>

#include "Keyboard/Config.h"
#include "Optimizer/DiffPlanner/CharDiff.h"
#include "Optimizer/DiffPlanner/DiffState.h"
#include "Types/Lines.h"

using namespace std;

namespace {

Lines toLines(const string& flat) { return Lines::unflatten(flat); }

// Best partition's diffs (empty when initial == goal).
vector<DiffState> bestDiffs(const Lines& initial, const Lines& goal, const Config& config) {
  vector<CharDiff::Plan> plans = CharDiff::calculate(initial, goal, config);
  return plans.empty() ? vector<DiffState>{} : plans.front().diffs;
}

// Applying CharDiff's diffs in sequence must reproduce the goal. This validates
// the DP reconstruction + DiffState production end-to-end, independent of the
// cost oracle.
void expectRoundTrip(const string& initial, const string& goal) {
  Config config = Config::uniform();
  Lines initLines = toLines(initial);
  Lines goalLines = toLines(goal);
  vector<DiffState> diffs = bestDiffs(initLines, goalLines, config);
  Lines applied = Myers::applyAllDiffState(diffs, initLines);
  EXPECT_EQ(applied.flatten(), goalLines.flatten())
      << "round-trip failed for \"" << initial << "\" -> \"" << goal << "\"";
}

TEST(CharDiffTest, RoundTripsHandcrafted) {
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
}

TEST(CharDiffTest, IdenticalBuffersProduceNoDiffs) {
  Config config = Config::uniform();
  Lines lines = toLines("hello world\nsecond line");
  EXPECT_TRUE(CharDiff::calculate(lines, lines, config).empty());
  EXPECT_TRUE(bestDiffs(lines, lines, config).empty());
}

TEST(CharDiffTest, TrailingDeletionIsSingleRegion) {
  Config config = Config::uniform();
  vector<DiffState> diffs =
      bestDiffs(toLines("hello world"), toLines("hello"), config);
  ASSERT_EQ(diffs.size(), 1u);
  EXPECT_TRUE(diffs[0].isPureDeletion());
  EXPECT_EQ(diffs[0].deletedText, " world");
}

TEST(CharDiffTest, InteriorInsertionIsSingleRegion) {
  Config config = Config::uniform();
  vector<DiffState> diffs =
      bestDiffs(toLines("abc"), toLines("abXc"), config);
  ASSERT_EQ(diffs.size(), 1u);
  EXPECT_TRUE(diffs[0].isPureInsertion());
  EXPECT_EQ(diffs[0].insertedText, "X");
}

TEST(CharDiffTest, RoundTripsRandomSmall) {
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
    if (Myers::applyAllDiffState(diffs, ol).flatten() != gl.flatten()) {
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
TEST(CharDiffTest, AllPlansRoundTripAscendAndDiffer) {
  Config config = Config::uniform();
  const vector<pair<string, string>> cases = {
      {"aaa bbb ccc", "xxx bbb yyy"}, {"abc xyz", "aXc xYz"},
      {"ab cd ef", "Xb cd eY"},       {"the quick fox", "the slow fox"},
      {"hello world", "hello"},       {"foo", "bar"},
      {"aaa\nbbb", "aaa bbb"},        {"x + x", "y + y"},
  };
  CharDiff::CostOptions options;
  options.maxPlans = 6;
  for (const auto& [init, goal] : cases) {
    Lines ol = toLines(init), gl = toLines(goal);
    vector<CharDiff::Plan> plans = CharDiff::calculate(ol, gl, config, options);
    ASSERT_FALSE(plans.empty()) << init << " -> " << goal;

    double prev = -1.0;
    set<string> signatures;
    for (size_t p = 0; p < plans.size(); p++) {
      EXPECT_GE(plans[p].cost, prev - 1e-9)
          << "costs not ascending at plan " << p << " for \"" << init << "\"";
      prev = plans[p].cost;

      Lines applied = Myers::applyAllDiffState(plans[p].diffs, ol);
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

}  // namespace
