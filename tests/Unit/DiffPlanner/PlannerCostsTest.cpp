#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Effort/RunningEffort.h"
#include "Keyboard/Config.h"
#include "Optimizer/DiffPlanner/PlannerCosts.h"
#include "Types/CountPrefixLimits.h"
#include "Types/Lines.h"

using namespace std;
using namespace VimDiff;

namespace {

TilingCost oracle(const FlatText& text, TilingCost::Kind kind) {
  return TilingCost(text, 1.0, CountPrefixLimits::DEFAULT_MAX_PREFIX_COUNT, kind);
}

// Single-command tilings whose cheapest form is unambiguous under uniform key
// costs: counted alternatives always pay at least one extra digit.
TEST(PlannerCostsTest, MoveTilingsMatchTheirCheapestCommand) {
  FlatText text(Lines{"one two three four", "next"});
  TilingCost move = oracle(text, TilingCost::Kind::Move);
  EXPECT_DOUBLE_EQ(move.query(0, 0), 0.0);
  EXPECT_DOUBLE_EQ(move.query(0, 1), MOVE_KEYS[CHAR]);      // l
  EXPECT_DOUBLE_EQ(move.query(0, 4), MOVE_KEYS[WORD]);      // w
  EXPECT_DOUBLE_EQ(move.query(0, 18), TO_LINE_END_KEYS);    // $ (to the newline)
  EXPECT_DOUBLE_EQ(move.query(0, 19), MOVE_KEYS[LINE]);     // j
}

TEST(PlannerCostsTest, DeleteTilingsMatchTheirCheapestCommand) {
  FlatText text(Lines{"one two", "three"});
  TilingCost del = oracle(text, TilingCost::Kind::Delete);
  EXPECT_DOUBLE_EQ(del.query(0, 1), DELETE_KEYS[CHAR]);     // x
  EXPECT_DOUBLE_EQ(del.query(0, 4), DELETE_KEYS[WORD]);     // dw
  EXPECT_DOUBLE_EQ(del.query(0, 7), TO_LINE_END_KEYS);      // D
  EXPECT_DOUBLE_EQ(del.query(0, 8), DELETE_KEYS[LINE]);     // dd
}

TEST(PlannerCostsTest, CountedTilingsPriceTheCountPrefix) {
  FlatText text(Lines{"a b c d e f g h"});
  TilingCost move = oracle(text, TilingCost::Kind::Move);
  // Six words: `6w` against a chain of single `w`s.
  const double counted = MOVE_KEYS[WORD] + countPrefixCost<CountClass::MovementWord>(6, 1.0);
  EXPECT_DOUBLE_EQ(move.query(0, 12), min(counted, 6 * MOVE_KEYS[WORD]));
}

// Splitting a tiling at an edge costs at most that edge's stop + start slack.
// Sealing relies on exactly this bound to cut deletions at a run's ends.
TEST(PlannerCostsTest, SlacksBoundTheCostOfSplittingAtAnEdge) {
  const vector<string> texts = {
      "one two three four",
      "aa bb\ncc dd\n\nee ff",
      "foo(bar, baz);\n  return x;\n",
  };
  for (const string& raw : texts) {
    FlatText text(Lines::unflatten(raw));
    const int N = (int)text.text.size();
    for (TilingCost::Kind kind : {TilingCost::Kind::Delete, TilingCost::Kind::Move}) {
      TilingCost cost = oracle(text, kind);
      for (int a = 0; a < N; a++)
        for (int c = a + 1; c <= N; c++)
          for (int b = a; b <= c; b++) {
            const double whole = cost.query(a, c);
            const double split = cost.query(a, b) + cost.query(b, c);
            EXPECT_LE(split, whole + cost.stopSlack(b) + cost.startSlack(b) + 1e-9)
                << (kind == TilingCost::Kind::Delete ? "delete" : "move") << " [" << a << ","
                << b << "," << c << ") in \"" << raw << "\"";
          }
    }
  }
}

TEST(PlannerCostsTest, TypingPrefixSumsMatchWholeSequenceEffort) {
  Config config = Config::uniform();
  const string goal = "hello world";
  Typing typing(FlatText(Lines{goal}), config);
  EXPECT_DOUBLE_EQ(typing.ins(0, (int)goal.size()), getEffort(goal, config));
  EXPECT_DOUBLE_EQ(typing.entry, getEffort("i", config));
  EXPECT_DOUBLE_EQ(typing.esc, getEffort("<Esc>", config));
  EXPECT_DOUBLE_EQ(typing.ins(3, 3), 0.0);
}

}  // namespace
