// Property: NavOptimizer results for generated sub-buffer navigation problems
// must replay in Neovim on the full buffer and land at the requested sub-buffer
// target without relying on motion semantics outside the boundary.

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Boundary/NavBoundary.h"
#include "Interpreter/MovementInterpreter.h"
#include "Keyboard/Config.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Property/PropertyDomains.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/NavContext.h"
#include "Utils/NeovimOracle.h"
#include "Utils/OracleReplay.h"

using namespace std;

namespace {

int clampedIndex(int value, int size) {
  return std::clamp(value, 0, size - 1);
}

struct EmbeddedMotionTest {
  Lines fullBuffer;
  Lines subBuffer;
  CursorPos subStart;
  CursorPos subEnd;
  CursorPos fullStart;
  int subBufferStartLine = 0;
  NavBoundary boundary;
};

struct EmbeddedMotionSpec {
  vector<string> fullBuffer;
  int subBufferStartLine;
  int subLineCount;
  int subStartIndex;
  int subEndIndex;
};

auto EmbeddedMotionSpecDomain() {
  return fuzztest::StructOf<EmbeddedMotionSpec>(
      PropertyDomains::LineVecDomain(1, 8, 0, 30),
      fuzztest::InRange<int>(0, 7),
      fuzztest::InRange<int>(1, 4),
      fuzztest::InRange<int>(0, 240),
      fuzztest::InRange<int>(0, 240));
}

class NavOptimizerGeneratedPropertyTest {
public:
  void SubBufferMotionCorrectness(const EmbeddedMotionSpec& spec) {
    auto test = buildEmbeddedTest(spec);
    if (test.subStart == test.subEnd) return;

    auto results =
        runOnSubBuffer(test.subBuffer, test.subStart, test.subEnd, test.boundary);
    ASSERT_FALSE(results.empty()) << "NavOptimizer returned no results"
                                  << "\n" << formatSpec(spec);

    for (const auto& result : results) {
      expectResultMatchesNeovim(test, spec, result);
    }
  }

private:
  NavContext navContext_{};
  NeovimOracle oracle_{};

  EmbeddedMotionTest buildEmbeddedTest(const EmbeddedMotionSpec& spec) {
    EmbeddedMotionTest test;
    test.fullBuffer = Lines(spec.fullBuffer);
    test.subBufferStartLine =
        clampedIndex(spec.subBufferStartLine, static_cast<int>(test.fullBuffer.size()));
    int endLine = min(
        test.subBufferStartLine + spec.subLineCount - 1,
        test.fullBuffer.lastLine());

    test.subBuffer =
        test.fullBuffer.getLineRange(test.subBufferStartLine, endLine + 1);
    test.subStart = test.subBuffer.cursorFromFlatIndexClamped(spec.subStartIndex);
    test.subEnd = test.subBuffer.cursorFromFlatIndexClamped(spec.subEndIndex);
    test.fullStart = CursorPos(test.subBufferStartLine + test.subStart.line,
                               test.subStart.col);

    CursorPos firstPos(test.subBufferStartLine, 0);
    CursorPos endPos(endLine, test.fullBuffer[endLine].effectiveSize());
    test.boundary = NavBoundary(test.fullBuffer, firstPos, endPos);

    return test;
  }

  string formatSpec(const EmbeddedMotionSpec& spec) {
    ostringstream out;
    out << "rawSubBufferStartLine=" << spec.subBufferStartLine
        << " rawSubLineCount=" << spec.subLineCount
        << " rawSubStartIndex=" << spec.subStartIndex
        << " rawSubEndIndex=" << spec.subEndIndex;
    return out.str();
  }

  vector<LandingResult> runOnSubBuffer(const Lines& subBuffer, CursorPos start,
                                       CursorPos end,
                                       const NavBoundary& boundary) {
    NavOptimizer opt(Config::uniform());
    return opt.optimize(subBuffer, start, end, {}, "", boundary, navContext_)
        .getResults();
  }

  void expectResultMatchesNeovim(const EmbeddedMotionTest& test,
                                 const EmbeddedMotionSpec& spec,
                                 const LandingResult& result) {
    const auto& seq = result.getSequence();
    SCOPED_TRACE(::testing::Message()
                 << "seq='" << seq << "'"
                 << " subStart=" << test.subStart << " subEnd=" << test.subEnd
                 << " fullStart=" << test.fullStart
                 << " subBufferStartLine=" << test.subBufferStartLine
                 << " " << formatSpec(spec)
                 << " hasLinesAbove=" << test.boundary.hasLinesAbove()
                 << " hasLinesBelow=" << test.boundary.hasLinesBelow()
                 << "\nfullBuffer=" << test.fullBuffer
                 << "\nsubBuffer=" << test.subBuffer);

    CursorPos ourEnd =
        simulateMovements(test.subStart, seq.view(), test.subBuffer);
    EXPECT_EQ(ourEnd, test.subEnd)
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
    .WithDomains(EmbeddedMotionSpecDomain());
