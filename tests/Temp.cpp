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

