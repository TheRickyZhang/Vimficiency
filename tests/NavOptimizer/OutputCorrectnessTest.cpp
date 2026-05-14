// tests/NavOptimizer/OutputCorrectnessTest.cpp
//
// Generated property tests for NavOptimizer motion-position correctness.
// Each emitted motion sequence is checked against Neovim on the full buffer.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="NavOptimizerOutputCorrectness.*"

#include <gtest/gtest.h>

#include "Interpreter/MovementInterpreter.h"
#include "Types/NavContext.h"
#include "Keyboard/Config.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Boundary/NavBoundary.h"
#include "Types/Lines.h"
#include "Utils/GeneratedProperty.h"
#include "Utils/NeovimOracle.h"
#include "Utils/RandomGeneration.h"

#include <exception>
#include <memory>

using namespace std;

// =============================================================================
// Test Infrastructure
// =============================================================================

// Test case structure for embedded sub-buffer testing
struct EmbeddedMotionTest {
  Lines fullBuffer;       // Complete buffer sent to Neovim
  Lines subBuffer;        // Extracted region for optimizer
  CursorPos subStart;      // Start position in sub-buffer coords
  CursorPos fullStart;     // Same position in full-buffer coords
  int subBufferStartLine; // Line offset of sub-buffer within full buffer
  NavBoundary boundary;
};

class NavOptimizerOutputCorrectness : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  static NavContext navContext;

  static void SetUpTestSuite() {
    oracle = make_unique<NeovimOracle>();
    navContext = NavContext();
  }

  static void TearDownTestSuite() {
    oracle.reset();
  }

  // Generate a random embedded sub-buffer scenario
  static EmbeddedMotionTest generateEmbeddedTest(int fullLines, int subLines) {
    EmbeddedMotionTest test;

    // Character pool - mixed content for realistic word boundaries
    const string chars = "abcd .,";

    // Generate full buffer
    for (int i = 0; i < fullLines; i++) {
      int len = RandomGen::range(10, 30);
      string line;
      for (int j = 0; j < len; j++) {
        line += RandomGen::pick(chars);
      }
      test.fullBuffer.push_back(line);
    }

    // Pick sub-buffer region: lines [startLine, endLine]
    test.subBufferStartLine = RandomGen::range(0, max(0, fullLines - subLines));
    int endLine = min(test.subBufferStartLine + subLines - 1, fullLines - 1);

    // Extract sub-buffer
    for (int i = test.subBufferStartLine; i <= endLine; i++) {
      test.subBuffer.push_back(test.fullBuffer[i]);
    }

    // Random starting position within sub-buffer
    int subLine = RandomGen::range(0, static_cast<int>(test.subBuffer.size()) - 1);
    int maxCol = test.subBuffer[subLine].empty() ? 0 : static_cast<int>(test.subBuffer[subLine].size()) - 1;
    int col = RandomGen::range(0, max(0, maxCol));

    test.subStart = CursorPos(subLine, col);
    test.fullStart = CursorPos(test.subBufferStartLine + subLine, col);

    // Set up boundary from full buffer positions
    CursorPos firstPos(test.subBufferStartLine, 0);
    CursorPos endPos(endLine, test.fullBuffer[endLine].effectiveSize());
    test.boundary = NavBoundary(test.fullBuffer, firstPos, endPos);

    return test;
  }

  // Convert full-buffer position to sub-buffer position (with bounds checking)
  static pair<bool, CursorPos> toSubBufferPos(const CursorPos& fullPos, int subBufferStartLine, int subBufferLines) {
    int subLine = fullPos.line - subBufferStartLine;
    if (subLine < 0 || subLine >= subBufferLines) {
      return {false, CursorPos(0, 0)};
    }
    return {true, CursorPos(subLine, fullPos.col)};
  }

  // Run optimizer on sub-buffer
  static vector<LandingResult> runOnSubBuffer(const Lines& subBuffer, CursorPos start, CursorPos end,
                                       const NavBoundary& boundary) {
    NavOptimizer opt(Config::uniform());
    return opt.optimize(subBuffer, start, end, {},
                        "jjjjjjjjjj", boundary, navContext).getResults();
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
      neovimResult = oracle->simulate(
          test.fullBuffer, test.fullStart.line, test.fullStart.col, seq.str());
    } catch (const exception& e) {
      oracle->restart();
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

unique_ptr<NeovimOracle> NavOptimizerOutputCorrectness::oracle;
NavContext NavOptimizerOutputCorrectness::navContext;

// =============================================================================
// Sub-buffer generated properties
// =============================================================================

TEST_F(NavOptimizerOutputCorrectness, GeneratedProperty_SubBufferMotionCorrectness) {
  GeneratedProperty::check({"Nav sub-buffer motion correctness", 42, 50}, [&](int) {
    auto test = generateEmbeddedTest(8, 4);

    int endLine = RandomGen::range(0, static_cast<int>(test.subBuffer.size()) - 1);
    int maxEndCol = test.subBuffer[endLine].empty() ? 0 : static_cast<int>(test.subBuffer[endLine].size()) - 1;
    CursorPos subEnd(endLine, RandomGen::range(0, max(0, maxEndCol)));

    auto results = runOnSubBuffer(test.subBuffer, test.subStart, subEnd, test.boundary);
    ASSERT_FALSE(results.empty()) << "NavOptimizer returned no results";

    for (const auto& result : results) {
      expectResultMatchesNeovim(test, subEnd, result);
    }
  });
}
