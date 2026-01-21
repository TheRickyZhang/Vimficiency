// Temp.cpp - Scratch test file for quick experiments
//
// This file is for temporary tests during development.
// Tests here may be incomplete or experimental.

#include <gtest/gtest.h>

#include "Editor/Edit.h"
#include "Editor/NavContext.h"
#include "Optimizer/Config.h"
#include "Optimizer/EditOptimizer.h"
#include "Boundary/EditBoundary.h"
#include "Utils/NeovimOracle.h"

using namespace std;

class TempTest : public ::testing::Test {
protected:
  Config config = Config::uniform();

  EditOptimizer makeOptimizer() {
    return EditOptimizer(config, OptimizerParams(30, 1e5, 1.0, 2.0));
  }

  // Create boundary for full buffer deletion (no constraints)
  EditBoundary makeFullBufferBoundary(const Lines& source) {
    if (source.empty()) {
      return EditBoundary(source, Position(0, 0), Position(0, 0));
    }
    int lastLine = static_cast<int>(source.size()) - 1;
    int lastCol = source[lastLine].empty() ? 0 : static_cast<int>(source[lastLine].size()) - 1;
    return EditBoundary(source, Position(0, 0), Position(lastLine, lastCol));
  }
};


TEST_F(TempTest, Placeholder) {
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

