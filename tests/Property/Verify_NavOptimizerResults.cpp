// Property: NavOptimizer results for generated sub-buffer navigation problems
// must replay in Neovim on the full buffer and land at the requested sub-buffer
// target without relying on motion semantics outside the boundary.

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Boundary/NavBoundary.h"
#include "Interpreter/MovementInterpreter.h"
#include "Keyboard/Config.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Property/PropertyTestUtils.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/NavContext.h"
#include "Utils/NeovimOracle.h"
#include "Utils/OracleReplay.h"
#include "Utils/RandomBufferHelpers.h"
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
    runSeedDriverCases(seed, 50, [&] {
      auto test = generateEmbeddedTest(8, 4);

      CursorPos subEnd = randomPos(test.subBuffer);

      auto results =
          runOnSubBuffer(test.subBuffer, test.subStart, subEnd, test.boundary);
      ASSERT_FALSE(results.empty()) << "NavOptimizer returned no results";

      for (const auto& result : results) {
        expectResultMatchesNeovim(test, subEnd, result);
      }
    });
  }

private:
  NavContext navContext_{};
  NeovimOracle oracle_{};

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

    test.subStart = randomPos(test.subBuffer);
    test.fullStart = CursorPos(test.subBufferStartLine + test.subStart.line,
                               test.subStart.col);

    CursorPos firstPos(test.subBufferStartLine, 0);
    CursorPos endPos(endLine, test.fullBuffer[endLine].effectiveSize());
    test.boundary = NavBoundary(test.fullBuffer, firstPos, endPos);

    return test;
  }

  vector<LandingResult> runOnSubBuffer(const Lines& subBuffer, CursorPos start,
                                       CursorPos end,
                                       const NavBoundary& boundary) {
    NavOptimizer opt(Config::uniform());
    return opt.optimize(subBuffer, start, end, {}, "", boundary, navContext_)
        .getResults();
  }

  void expectResultMatchesNeovim(const EmbeddedMotionTest& test,
                                 CursorPos subEnd,
                                 const LandingResult& result) {
    const auto& seq = result.getSequence();
    SCOPED_TRACE(::testing::Message()
                 << "seq='" << seq << "'"
                 << " subStart=" << test.subStart << " subEnd=" << subEnd
                 << " fullStart=" << test.fullStart
                 << " subBufferStartLine=" << test.subBufferStartLine
                 << " hasLinesAbove=" << test.boundary.hasLinesAbove()
                 << " hasLinesBelow=" << test.boundary.hasLinesBelow()
                 << "\nfullBuffer=" << test.fullBuffer
                 << "\nsubBuffer=" << test.subBuffer);

    CursorPos ourEnd =
        simulateMovements(test.subStart, seq.view(), test.subBuffer);
    EXPECT_EQ(ourEnd, subEnd)
        << "NavOptimizer result did not reach requested goal";

    CursorPos expectedFullEnd(test.subBufferStartLine + ourEnd.line,
                              ourEnd.col);
    OracleReplay::expectMatchesOracle(oracle_, test.fullBuffer, test.fullStart,
                                      seq.str(), test.fullBuffer,
                                      expectedFullEnd);
  }
};

} // namespace

FUZZ_TEST_F(NavOptimizerGeneratedPropertyTest, SubBufferMotionCorrectness)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000));
