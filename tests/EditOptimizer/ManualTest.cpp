// tests/EditOptimizer/ManualTest.cpp
//
// Manual tests for EditOptimizer with hardcoded setups.
// Tests boundary constraints, replacement strategies, and specific behaviors.
// For random/stress tests, see OutputCorrectnessTest.cpp.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="EditOptimizer_ManualTest.*"

#include <climits>
#include <gtest/gtest.h>
#include <memory>

#include "Editor/Edit.h"
#include "Editor/Mode.h"
#include "Editor/Position.h"
#include "Optimizer/Config.h"
#include "Optimizer/EditOptimizer/EditOptimizer.h"
#include "Boundary/EditBoundary.h"
#include "Utils/Lines.h"
// #include "Utils/TestUtils.h"
#include "Utils/NeovimOracle.h"

using namespace std;

class EditOptimizer_ManualTest : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  Config config = Config::uniform();
  // For correctness on small buffers, set int max upper bound
  EditOptimizerParams params = EditOptimizerParams{}.withMaxResults(INT_MAX);
  EditOptimizer opt{config};

  static void SetUpTestSuite() { oracle = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }
};

unique_ptr<NeovimOracle> EditOptimizer_ManualTest::oracle;

// =============================================================================
// Test Helpers
// =============================================================================

struct ApplyResult {
  Lines lines;
  Position pos;
  Mode mode;

  ApplyResult(Lines lines, Position pos, Mode mode = Mode::Normal)
      : lines(std::move(lines)), pos(pos), mode(mode) {}
};

ApplyResult applySequence(const Lines& source, Position initialPos, const string& sequence) {
  ApplyResult result(source, initialPos);
  string lastEditCmd;
  for (const auto& op : Edit::parseEdits(sequence)) {
    Edit::applyEdit(result.lines, result.pos, result.mode, op, &lastEditCmd);
  }
  return result;
}

bool cursorStateMatches(const ApplyResult& ours, const SimulationResult& nvim) {
  return ours.pos.line == nvim.row && ours.pos.col == nvim.col && ours.mode == nvim.mode;
}

SimulationResult verifySequenceWithOracle(
    NeovimOracle* oracle,
    const Lines& source,
    Position initialPos,
    const string& sequence) {
  SimulationResult nvim = oracle->simulate(source, initialPos.line, initialPos.col, sequence);
  ApplyResult ours = applySequence(source, initialPos, sequence);

  EXPECT_EQ(ours.lines, nvim.lines)
      << "Lines mismatch for seq='" << sequence << "' from " << initialPos << "\n"
      << "  Source: " << source << "\n"
      << "  Ours:   " << ours.lines << "\n"
      << "  Neovim: " << nvim.lines;

  EXPECT_TRUE(cursorStateMatches(ours, nvim))
      << "Cursor mismatch for seq='" << sequence << "'\n"
      << "  Ours:   " << ours.pos << " mode=" << static_cast<int>(ours.mode) << "\n"
      << "  Neovim: (" << nvim.row << "," << nvim.col << ") mode=" << static_cast<int>(nvim.mode);

  return nvim;
}

bool allPositionsValid(const vector<Result>& results, const Lines& source) {
  int idx = 0;
  for (int r = 0; r < static_cast<int>(source.size()); r++) {
    for (int c = 0; c < source[r].effectiveSize(); c++) {
      if (!results[idx++].isValid()) {
        return false;
      }
    }
  }
  return true;
}

template<typename Fn>
void forEachValidResult(const vector<Result>& results, const Lines& lines, Fn fn) {
  int idx = 0;
  for (int r = 0; r < static_cast<int>(lines.size()); r++) {
    for (int c = 0; c < lines[r].effectiveSize(); c++) {
      const Result& result = results[idx++];
      if (result.isValid()) {
        fn(Position(r, c), result.sequence);
      }
    }
  }
}

// =============================================================================
// Pure Deletion Tests (full buffer, no boundaries)
// =============================================================================

TEST_F(EditOptimizer_ManualTest, PureDeletion_OracleVerified) {
  // Single test with oracle verification - stress tests cover more shapes
  Lines lines = {"aa", "bb"};
  EditResult editRes = opt.optimizePureDeletion(lines, EditBoundary(lines, Position(0, 0), lines.endPos()), params);
  const vector<Result>& res = editRes.getResults();

  EXPECT_TRUE(allPositionsValid(res, lines));

  forEachValidResult(res, lines, [&](Position pos, const auto& seq) {
    SimulationResult nvimRes = verifySequenceWithOracle(oracle.get(), lines, pos, seq.str());
    EXPECT_TRUE(nvimRes.lines.isEmpty() && nvimRes.mode == Mode::Normal)
        << "Sequence '" << seq << "' from " << pos << " did not reach goal";
  });
}

// =============================================================================
// Boundary-Constrained Tests
// =============================================================================

TEST_F(EditOptimizer_ManualTest, Boundary_LinesBelow) {
  // Edit region has lines below - tests hasLinesBelow constraint
  Lines fullBuffer = {"aa", "bb", "xx"};
  Position initialPos(0, 0), endPos(1, 2);
  Lines editRegion = fullBuffer.getSpan(initialPos, endPos);
  EditBoundary boundary(fullBuffer, initialPos, endPos);

  EditResult res = opt.optimizeEdit(editRegion, {""}, boundary, params);
  EXPECT_TRUE(allPositionsValid(res.getResults(), editRegion));
}

TEST_F(EditOptimizer_ManualTest, Boundary_SingleLineSurrounded) {
  // Single line edit region surrounded by other lines
  // Can't use dd - must use S/cc
  Lines fullBuffer = {"xx", "hello", "xx"};
  Position initialPos(1, 0), endPos(1, 5);
  Lines editRegion = fullBuffer.getSpan(initialPos, endPos);
  EditBoundary boundary(fullBuffer, initialPos, endPos);

  EditResult res = opt.optimizeEdit(editRegion, {""}, boundary, params);
  // printResultsDebug(res.typeAllResults, "boundary line surrounded");
  EXPECT_TRUE(allPositionsValid(res.getResults(), editRegion));
}

TEST_F(EditOptimizer_ManualTest, Boundary_LinewiseCursorContainment) {
  // Verify cursor stays within edit region and surrounding lines unchanged
  Lines fullBuffer = {"xx", "aa", "bb", "yy"};
  Position initialPos(1, 0), endPos(2, 2);
  Lines editRegion = fullBuffer.getSpan(initialPos, endPos);
  EditBoundary boundary(fullBuffer, initialPos, endPos);

  EditResult res = opt.optimizeEdit(editRegion, {""}, boundary, params);

  forEachValidResult(res.getResults(), editRegion, [&](Position pos, const auto& seq) {
    // Skip visual mode sequences for now
    if (!seq.empty() && seq.view()[0] == 'v') return;

    Position fullBufferPos(pos.line + initialPos.line, pos.col);
    ApplyResult applied = applySequence(fullBuffer, fullBufferPos, seq.str());

    EXPECT_EQ(applied.lines[0], "xx") << "Line above modified after '" << seq << "'";
    EXPECT_EQ(applied.lines.back(), "yy") << "Line below modified after '" << seq << "'";
    EXPECT_EQ(applied.pos.line, 1)
        << "Cursor escaped edit region! Pos=" << applied.pos << " after '" << seq << "'";
  });
}


// =============================================================================
// Note: Stress tests (random buffers) are in OutputCorrectnessTest.cpp
// =============================================================================
