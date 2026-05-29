// tests/Unit/NavOptimizer/BufferIndexTest.cpp
//
// BufferIndex's word landings drive counted word-motion (Nw/Nb) candidates via
// NavExplorer's getClosestInRange. A truly empty line is a word/WORD in Vim
// (w/W and b/B stop on it), so it must appear as a WordBegin/BigWordBegin
// landing; otherwise the count<->landing mapping diverges from where Nw lands.
//
// Run: ./build/tests/vimfy_unit_tests --gtest_filter="BufferIndexEmptyLine.*"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "Optimizer/NavOptimizer/BufferIndex.h"
#include "Types/Lines.h"
#include "Types/Pos.h"
#include "Utils/NeovimOracle.h"

class BufferIndexEmptyLine : public ::testing::Test {
protected:
  static std::unique_ptr<NeovimOracle> oracle;
  static void SetUpTestSuite() { oracle = std::make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }

  static int countForPos(const std::vector<RepeatMovementResult>& rs, Pos p) {
    for (const auto& r : rs)
      if (r.pos == p) return r.count;
    return -1;
  }
};
std::unique_ptr<NeovimOracle> BufferIndexEmptyLine::oracle;

TEST_F(BufferIndexEmptyLine, WordBeginCountsMatchOracleAcrossEmptyLine) {
  Lines lines = {"a b c d", "", "e f"};
  BufferIndex idx(lines);

  // Ground truth from Neovim: where 4w / 5w land from (0,0). 4w stops on the
  // empty line; 5w reaches the next content line.
  SimulationResult w4 = oracle->simulate(lines, 0, 0, "4w");
  SimulationResult w5 = oracle->simulate(lines, 0, 0, "5w");

  auto wordBegins = idx.getClosestInRange<true>(
      LandingType::WordBegin, Pos(0, 0), Pos(0, 2), Pos(2, 2));
  EXPECT_EQ(countForPos(wordBegins, Pos(w4.row, w4.col)), 4);
  EXPECT_EQ(countForPos(wordBegins, Pos(w5.row, w5.col)), 5);

  // Same for BigWord (all single-char words here, so W behaves like w).
  SimulationResult W4 = oracle->simulate(lines, 0, 0, "4W");
  auto bigWordBegins = idx.getClosestInRange<true>(
      LandingType::BigWordBegin, Pos(0, 0), Pos(0, 2), Pos(2, 2));
  EXPECT_EQ(countForPos(bigWordBegins, Pos(W4.row, W4.col)), 4);
}

TEST_F(BufferIndexEmptyLine, WordEndDoesNotLandOnEmptyLine) {
  // `e` has no word-end on an empty line, so WordEnd must NOT include it.
  Lines lines = {"a b c d", "", "e f"};
  BufferIndex idx(lines);

  auto wordEnds = idx.getClosestInRange<true>(
      LandingType::WordEnd, Pos(0, 0), Pos(0, 2), Pos(2, 2));
  EXPECT_EQ(countForPos(wordEnds, Pos(1, 0)), -1);
}
