#include <algorithm>
#include <cstdint>
#include <string>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Interpreter/MovementInterpreter.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"

using namespace std;

namespace {

class MotionInvariantGeneratedPropertyTest {
 public:
  void HNeverIncreasesColumn(uint32_t seed) {
    runCases(seed, 200, [&] {
      auto [lines, cursor] = generateBufferAndCursor();
      CursorPos result = simulateMovements(cursor, "h", lines);
      EXPECT_LE(result.col, cursor.col);
      EXPECT_EQ(result.line, cursor.line);
    });
  }

  void LNeverDecreasesColumn(uint32_t seed) {
    runCases(seed, 200, [&] {
      auto [lines, cursor] = generateBufferAndCursor();
      CursorPos result = simulateMovements(cursor, "l", lines);
      EXPECT_GE(result.col, cursor.col);
      EXPECT_EQ(result.line, cursor.line);
    });
  }

  void JNeverDecreasesLine(uint32_t seed) {
    runCases(seed, 200, [&] {
      auto [lines, cursor] = generateBufferAndCursor();
      CursorPos result = simulateMovements(cursor, "j", lines);
      EXPECT_GE(result.line, cursor.line);
    });
  }

  void KNeverIncreasesLine(uint32_t seed) {
    runCases(seed, 200, [&] {
      auto [lines, cursor] = generateBufferAndCursor();
      CursorPos result = simulateMovements(cursor, "k", lines);
      EXPECT_LE(result.line, cursor.line);
    });
  }

  void GGAlwaysReachesFirstLine(uint32_t seed) {
    runCases(seed, 200, [&] {
      auto [lines, cursor] = generateBufferAndCursor();
      CursorPos result = simulateMovements(cursor, "gg", lines);
      EXPECT_EQ(result.line, 0);
    });
  }

  void GAlwaysReachesLastLine(uint32_t seed) {
    runCases(seed, 200, [&] {
      auto [lines, cursor] = generateBufferAndCursor();
      CursorPos result = simulateMovements(cursor, "G", lines);
      EXPECT_EQ(result.line, static_cast<int>(lines.size()) - 1);
    });
  }

 private:
  template <typename Fn>
  void runCases(uint32_t seed, int count, Fn&& fn) {
    RandomGen::seed(seed);
    for (int caseIndex = 0; caseIndex < count; caseIndex++) {
      SCOPED_TRACE(::testing::Message() << "seed=" << seed << " case=" << caseIndex);
      fn();
    }
  }

  pair<Lines, CursorPos> generateBufferAndCursor() {
    int lineCount = RandomGen::range(1, 12);
    Lines lines = randomLines(lineCount, 0, 30);
    int line = RandomGen::range(0, lineCount - 1);
    int maxCol = lines[line].empty()
        ? 0
        : static_cast<int>(lines[line].size()) - 1;
    int col = RandomGen::range(0, max(0, maxCol));
    return {std::move(lines), CursorPos(line, col)};
  }
};

}  // namespace

FUZZ_TEST_F(MotionInvariantGeneratedPropertyTest, HNeverIncreasesColumn)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({2001});

FUZZ_TEST_F(MotionInvariantGeneratedPropertyTest, LNeverDecreasesColumn)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({2002});

FUZZ_TEST_F(MotionInvariantGeneratedPropertyTest, JNeverDecreasesLine)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({2003});

FUZZ_TEST_F(MotionInvariantGeneratedPropertyTest, KNeverIncreasesLine)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({2004});

FUZZ_TEST_F(MotionInvariantGeneratedPropertyTest, GGAlwaysReachesFirstLine)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({2005});

FUZZ_TEST_F(MotionInvariantGeneratedPropertyTest, GAlwaysReachesLastLine)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({2006});
