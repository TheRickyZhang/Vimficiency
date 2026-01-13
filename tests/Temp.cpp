// Temp.cpp - Scratch test file for quick experiments
//
// This file is for temporary tests during development.
// Tests here may be incomplete or experimental.

#include <gtest/gtest.h>
#include <ostream>

#include "Optimizer/Config.h"
#include "Optimizer/EditOptimizer.h"
#include "Optimizer/EditBoundary.h"
#include "Utils/Lines.h"
#include "Utils/NeovimOracle.h"

using namespace std;

class TempTest : public ::testing::Test {
protected:
  Config config = Config::uniform();

  EditOptimizer makeOptimizer() {
    return EditOptimizer(config, OptimizerParams(30, 1e5, 1.0, 2.0), 3.0);
  }

  EditBoundary noBoundary() {
    EditBoundary b{};
    b.startsAtLineStart = true;
    b.endsAtLineEnd = true;
    return b;
  }
};


TEST_F(TempTest, Placeholder) {
  // Placeholder test - add experiments here
  EXPECT_TRUE(true);
}

// ============================================================================
// NeovimOracle Tests - Verify C++ simulation matches actual Neovim
// ============================================================================

class NeovimOracleTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    oracle_ = std::make_unique<NeovimOracle>();
  }
  static void TearDownTestSuite() {
    oracle_.reset();
  }
  static std::unique_ptr<NeovimOracle> oracle_;
};

std::unique_ptr<NeovimOracle> NeovimOracleTest::oracle_;

TEST_F(NeovimOracleTest, BasicMotion_w) {
  auto r = oracle_->simulate({"one two three"}, 0, 0, "w");
  EXPECT_EQ(r.lines, vector<string>{"one two three"});
  EXPECT_EQ(r.row, 0);
  EXPECT_EQ(r.col, 4);  // Start of "two"
  EXPECT_EQ(r.mode, Mode::Normal);
}

TEST_F(NeovimOracleTest, BasicMotion_dw) {
  auto r = oracle_->simulate({"one two three"}, 0, 0, "dw");
  EXPECT_EQ(r.lines, vector<string>{"two three"});
  EXPECT_EQ(r.row, 0);
  EXPECT_EQ(r.col, 0);
  EXPECT_EQ(r.mode, Mode::Normal);
}

TEST_F(NeovimOracleTest, ChangeEntersInsertMode) {
  auto r = oracle_->simulate({"one two three"}, 0, 0, "cw");
  EXPECT_EQ(r.lines, vector<string>{" two three"});
  EXPECT_EQ(r.row, 0);
  EXPECT_EQ(r.col, 0);
  EXPECT_EQ(r.mode, Mode::Insert);
}

TEST_F(NeovimOracleTest, CrossLineMotion_db) {
  auto r = oracle_->simulate({"ab", "cd"}, 1, 0, "db");
  EXPECT_EQ(r.lines, vector<string>{"cd"});
  EXPECT_EQ(r.row, 0);
  EXPECT_EQ(r.col, 0);
  EXPECT_EQ(r.mode, Mode::Normal);
}

