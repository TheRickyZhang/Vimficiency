// tests/Unit/Commands/FMotionGenerationTest.cpp
//
// Regression tests for VimCore::generateFMotions on lines containing
// non-ASCII bytes. The occurrence counter is an array<int,256> indexed by the
// raw byte; indexing it with a signed `char` makes any byte >= 0x80 a negative
// index -> out-of-bounds read/write. Reliably caught under ASan; the functional
// assertions below also pin the expected occurrence counts.
//
// Run: ./build/tests/vimfy_unit_tests --gtest_filter="FMotionGeneration.*"

#include <gtest/gtest.h>

#include <string>
#include <tuple>
#include <vector>

#include "VimCore/VimMotionUtils.h"

using std::get;
using std::string;

TEST(FMotionGeneration, ForwardCountsAsciiOccurrences) {
  // Sanity: ASCII path is unaffected.
  string line = "a b b c";
  auto res = VimCore::generateFMotions<true>(/*currCol=*/0, /*targetCol=*/6,
                                             line, /*threshold=*/100);
  // Window [1,6]: ' '(0), 'b'(0), ' '(1), 'b'(1), ' '(2), 'c'(0)
  ASSERT_EQ(res.size(), 6u);
  EXPECT_EQ(get<0>(res[1]), 'b');
  EXPECT_EQ(get<2>(res[1]), 0);
  EXPECT_EQ(get<0>(res[3]), 'b');
  EXPECT_EQ(get<2>(res[3]), 1);
}

TEST(FMotionGeneration, ForwardHandlesHighBytes) {
  // 'a', 0xC3, 0xC3, 'b' — 0xC3 is negative as a signed char.
  string line = {'a', '\xc3', '\xc3', 'b'};
  auto res = VimCore::generateFMotions<true>(/*currCol=*/0, /*targetCol=*/3,
                                             line, /*threshold=*/10);

  // Window [1,3]: first 0xC3 (occurrence 0), second 0xC3 (occurrence 1), 'b' (0).
  ASSERT_EQ(res.size(), 3u);
  EXPECT_EQ(get<0>(res[0]), '\xc3');
  EXPECT_EQ(get<1>(res[0]), 1);
  EXPECT_EQ(get<2>(res[0]), 0);

  EXPECT_EQ(get<0>(res[1]), '\xc3');
  EXPECT_EQ(get<1>(res[1]), 2);
  EXPECT_EQ(get<2>(res[1]), 1);

  EXPECT_EQ(get<0>(res[2]), 'b');
  EXPECT_EQ(get<1>(res[2]), 3);
  EXPECT_EQ(get<2>(res[2]), 0);
}

TEST(FMotionGeneration, BackwardHandlesHighBytes) {
  // Backward scan over the same high bytes.
  string line = {'a', '\xc3', '\xc3', 'b'};
  auto res = VimCore::generateFMotions<false>(/*currCol=*/3, /*targetCol=*/0,
                                              line, /*threshold=*/10);
  // Window [0,2] scanned right-to-left: 0xC3 (occ 0), 0xC3 (occ 1), 'a' (0).
  ASSERT_EQ(res.size(), 3u);
  EXPECT_EQ(get<0>(res[0]), '\xc3');
  EXPECT_EQ(get<2>(res[0]), 0);
  EXPECT_EQ(get<0>(res[1]), '\xc3');
  EXPECT_EQ(get<2>(res[1]), 1);
  EXPECT_EQ(get<0>(res[2]), 'a');
  EXPECT_EQ(get<2>(res[2]), 0);
}
