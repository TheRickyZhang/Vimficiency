#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Keyboard/Config.h"
#include "Optimizer/DiffPlanner/PlannerCosts.h"
#include "Optimizer/DiffPlanner/SealMatchedRuns.h"
#include "Optimizer/DiffPlanner/VimDiff.h"
#include "Types/Lines.h"

using namespace std;
using namespace VimDiff;

namespace {

vector<Block> seal(const string& initialText, const string& goalText) {
  Config config = Config::uniform();
  Lines initial = Lines::unflatten(initialText);
  Lines goal = Lines::unflatten(goalText);
  FlatText flatInitial(initial), flatGoal(goal);
  Typing typing(flatGoal, config);
  return sealMatchedRuns(flatInitial, flatGoal, typing, initial, goal, config, CostOptions{});
}

// Blocks are ordered and disjoint, and everything outside them is the same
// text at the same offsets on both sides — the invariant that makes the
// sealed partition a valid plan.
void expectSealedRunsMatch(const string& a, const string& b, const vector<Block>& blocks) {
  int ia = 0, ib = 0;
  for (const Block& blk : blocks) {
    ASSERT_LE(ia, blk.aBegin);
    ASSERT_LE(ib, blk.bBegin);
    ASSERT_LE(blk.aBegin, blk.aEnd);
    ASSERT_LE(blk.bBegin, blk.bEnd);
    EXPECT_EQ(blk.aBegin - ia, blk.bBegin - ib);
    EXPECT_EQ(a.substr(ia, blk.aBegin - ia), b.substr(ib, blk.bBegin - ib));
    ia = blk.aEnd;
    ib = blk.bEnd;
  }
  EXPECT_EQ(a.substr(ia), b.substr(ib));
}

TEST(SealMatchedRunsTest, LongIdenticalWordSealsIntoTwoBlocks) {
  const string run(200, 'x');
  const string a = "AAAAAAAAAA" + run + "BBBBBBBBBB";
  const string b = "CCCCCCCCCC" + run + "DDDDDDDDDD";
  vector<Block> blocks = seal(a, b);
  ASSERT_EQ(blocks.size(), 2u);
  EXPECT_EQ(blocks[0].aBegin, 0);
  EXPECT_LE(blocks[0].aEnd, 110);  // margins are bounded by half the run
  EXPECT_GE(blocks[1].aBegin, 110);
  EXPECT_EQ(blocks[1].aEnd, (int)a.size());
  expectSealedRunsMatch(a, b, blocks);
}

TEST(SealMatchedRunsTest, ShortRunStaysInsideOneBlock) {
  const string a = "AAAA ab BBBB";
  const string b = "CCCC ab DDDD";
  vector<Block> blocks = seal(a, b);
  ASSERT_EQ(blocks.size(), 1u);
  EXPECT_EQ(blocks[0].aBegin, 0);
  EXPECT_EQ(blocks[0].aEnd, (int)a.size());
  expectSealedRunsMatch(a, b, blocks);
}

TEST(SealMatchedRunsTest, KeptLinesSealAcrossLineBoundaries) {
  string kept;
  for (int i = 0; i < 40; i++) kept += "kept line " + to_string(i) + "\n";
  const string a = "AAAAAAAAAA\n" + kept + "BBBBBBBBBB";
  const string b = "CCCCCCCCCC\n" + kept + "DDDDDDDDDD";
  vector<Block> blocks = seal(a, b);
  ASSERT_EQ(blocks.size(), 2u);
  expectSealedRunsMatch(a, b, blocks);
}

// Text lying entirely inside a sealed run leaves nothing to plan. (Short
// identical text is not sealed and comes back as one identical block; `plan()`
// short-circuits identical inputs before sealing for that reason.)
TEST(SealMatchedRunsTest, TextInsideOneSealedRunHasNoBlocks) {
  const string run(200, 'x');
  EXPECT_TRUE(seal(run, run).empty());
}

}  // namespace
