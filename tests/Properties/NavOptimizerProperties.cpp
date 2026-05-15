#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Boundary/NavBoundary.h"
#include "Interpreter/MovementInterpreter.h"
#include "Keyboard/Config.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/NavContext.h"
#include "Utils/NeovimOracle.h"
#include "Utils/RandomGeneration.h"

using namespace std;

namespace {

struct EmbeddedMotionTest {
  Lines fullBuffer;
  Lines subBuffer;
  CursorPos subStart;
  CursorPos fullStart;
  int subBufferStartLine = 0;
  NavBoundary boundary;
};

class NavOptimizerGeneratedPropertyTest {
 public:
  void SubBufferMotionCorrectness(uint32_t seed) {
    runCases(seed, 50, [&] {
      auto test = generateEmbeddedTest(8, 4);

      int endLine = RandomGen::range(0, static_cast<int>(test.subBuffer.size()) - 1);
      int maxEndCol = test.subBuffer[endLine].empty()
          ? 0
          : static_cast<int>(test.subBuffer[endLine].size()) - 1;
      CursorPos subEnd(endLine, RandomGen::range(0, max(0, maxEndCol)));

      auto results = runOnSubBuffer(test.subBuffer, test.subStart, subEnd, test.boundary);
      ASSERT_FALSE(results.empty()) << "NavOptimizer returned no results";

      for (const auto& result : results) {
        expectResultMatchesNeovim(test, subEnd, result);
      }
    });
  }

 private:
  NavContext navContext_{};
  NeovimOracle oracle_{};

  template <typename Fn>
  void runCases(uint32_t seed, int count, Fn&& fn) {
    RandomGen::seed(seed);
    for (int caseIndex = 0; caseIndex < count; caseIndex++) {
      SCOPED_TRACE(::testing::Message() << "seed=" << seed << " case=" << caseIndex);
      fn();
    }
  }

  EmbeddedMotionTest generateEmbeddedTest(int fullLines, int subLines) {
    EmbeddedMotionTest test;
    const string chars = "abcd .,";

    for (int i = 0; i < fullLines; i++) {
      int len = RandomGen::range(10, 30);
      string line;
      for (int j = 0; j < len; j++) {
        line += RandomGen::pick(chars);
      }
      test.fullBuffer.push_back(line);
    }

    test.subBufferStartLine = RandomGen::range(0, max(0, fullLines - subLines));
    int endLine = min(test.subBufferStartLine + subLines - 1, fullLines - 1);

    for (int i = test.subBufferStartLine; i <= endLine; i++) {
      test.subBuffer.push_back(test.fullBuffer[i]);
    }

    int subLine = RandomGen::range(0, static_cast<int>(test.subBuffer.size()) - 1);
    int maxCol = test.subBuffer[subLine].empty()
        ? 0
        : static_cast<int>(test.subBuffer[subLine].size()) - 1;
    int col = RandomGen::range(0, max(0, maxCol));

    test.subStart = CursorPos(subLine, col);
    test.fullStart = CursorPos(test.subBufferStartLine + subLine, col);

    CursorPos firstPos(test.subBufferStartLine, 0);
    CursorPos endPos(endLine, test.fullBuffer[endLine].effectiveSize());
    test.boundary = NavBoundary(test.fullBuffer, firstPos, endPos);

    return test;
  }

  static pair<bool, CursorPos> toSubBufferPos(
      const CursorPos& fullPos, int subBufferStartLine, int subBufferLines) {
    int subLine = fullPos.line - subBufferStartLine;
    if (subLine < 0 || subLine >= subBufferLines) {
      return {false, CursorPos(0, 0)};
    }
    return {true, CursorPos(subLine, fullPos.col)};
  }

  vector<LandingResult> runOnSubBuffer(
      const Lines& subBuffer,
      CursorPos start,
      CursorPos end,
      const NavBoundary& boundary) {
    NavOptimizer opt(Config::uniform());
    return opt.optimize(subBuffer, start, end, {},
                        "jjjjjjjjjj", boundary, navContext_).getResults();
  }

  void expectResultMatchesNeovim(
      const EmbeddedMotionTest& test,
      CursorPos subEnd,
      const LandingResult& result) {
    const auto& seq = result.getSequence();
    SCOPED_TRACE(
        ::testing::Message()
            << "seq='" << seq << "'"
            << " subStart=" << test.subStart
            << " subEnd=" << subEnd
            << " fullStart=" << test.fullStart
            << " subBufferStartLine=" << test.subBufferStartLine
            << " hasLinesAbove=" << test.boundary.hasLinesAbove()
            << " hasLinesBelow=" << test.boundary.hasLinesBelow()
            << "\nfullBuffer=" << test.fullBuffer
            << "\nsubBuffer=" << test.subBuffer);

    SimulationResult neovimResult;
    try {
      neovimResult = oracle_.simulate(
          test.fullBuffer, test.fullStart.line, test.fullStart.col, seq.str());
    } catch (const exception& e) {
      oracle_.restart();
      FAIL() << "NeovimOracle failed for seq='" << seq << "': " << e.what();
    }

    CursorPos neovimEnd(neovimResult.row, neovimResult.col);
    CursorPos ourEnd = simulateMovements(test.subStart, seq.view(), test.subBuffer);
    auto [inBounds, neovimSubPos] = toSubBufferPos(
        neovimEnd, test.subBufferStartLine,
        static_cast<int>(test.subBuffer.size()));

    EXPECT_TRUE(inBounds)
        << "Neovim landed outside sub-buffer at " << neovimEnd;
    if (inBounds) {
      EXPECT_EQ(ourEnd, neovimSubPos)
          << "Our simulator and Neovim disagree on landing position"
          << "\n  ourEnd=" << ourEnd
          << "\n  neovimEndFull=" << neovimEnd
          << "\n  neovimEndSub=" << neovimSubPos;
    }
  }
};

}  // namespace

FUZZ_TEST_F(NavOptimizerGeneratedPropertyTest, SubBufferMotionCorrectness)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({42});
