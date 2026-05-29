// tests/Unit/Misc/IndentationTest.cpp
//
// Regression tests for Optimizer/Indentation.h.
//
// shiftwidth==0 is a legal Neovim setting ("follow tabstop"). It must never
// reach bsCountForIndent's `((pos-1)/sw)*sw` as a zero divisor (integer
// divide-by-zero -> SIGFPE, crashing the host nvim process through the FFI).
//
// Run: ./build/tests/vimfy_unit_tests --gtest_filter="IndentationTest.*"

#include <gtest/gtest.h>

#include "Optimizer/Indentation.h"

// The crash reproducer: reducing indent (from > to) is the path that enters
// the divide loop. With sw==0 this must not divide by zero.
TEST(IndentationTest, BsCountForIndentSurvivesZeroShiftwidth) {
  EXPECT_EQ(bsCountForIndent(/*from=*/8, /*to=*/2, /*sw=*/0), -1);
}

TEST(IndentationTest, BsCountForIndentNegativeShiftwidth) {
  EXPECT_EQ(bsCountForIndent(/*from=*/8, /*to=*/0, /*sw=*/-1), -1);
}

// Guard must not regress the normal (sw > 0) behavior.
TEST(IndentationTest, BsCountForIndentNormalBoundaries) {
  EXPECT_EQ(bsCountForIndent(16, 8, 8), 1);   // 16 -> 8 in one BS
  EXPECT_EQ(bsCountForIndent(8, 0, 8), 1);    // 8  -> 0 in one BS
  EXPECT_EQ(bsCountForIndent(8, 2, 8), -1);   // can't land on 2 from a sw=8 boundary
  EXPECT_EQ(bsCountForIndent(2, 2, 8), 0);    // already there
}
