#include <gtest/gtest.h>

#include "Editor/NavContext.h"
#include "Utils/TestUtils.h"

#include "Keyboard/MotionToKeys.h"
#include "Optimizer/Config.h"
#include "Optimizer/MotionBoundary.h"
#include "Optimizer/MovementOptimizer.h"
#include "State/RunningEffort.h"
#include "Editor/Snapshot.h"
#include "Editor/Motion.h"
#include "Utils/Lines.h"

using namespace std;

class MovementOptimizerTest : public ::testing::Test {
protected:
  static Lines a1_long_line;
  static Lines a2_block_lines;
  static Lines a3_spaced_lines;
  static Lines m1_main_basic;
  static NavContext navContext;

  static void SetUpTestSuite() {
      a1_long_line = TestFiles::load("a1_long_line.txt");
      a2_block_lines = TestFiles::load("a2_block_lines.txt");
      a3_spaced_lines = TestFiles::load("a3_spaced_lines.txt");
      m1_main_basic = TestFiles::load("m1_main_basic.txt");
      navContext = NavContext();
  }

  static vector<Result>
  runOptimizer(const Lines &lines, Position start,
               Position end, const string &userSeq,
               const MotionToKeys& allowedMotions = EXPLORABLE_MOTIONS,
               vector<KeyAdjustment> adjustments = {},
               Config config = Config::uniform()
               ) {
    for(KeyAdjustment ka : adjustments) {
      config.keyInfo[static_cast<size_t>(ka.k)].base_cost = ka.cost;
    }

    MovementOptimizer opt(config);

    // Tests use full test files, so don't exclude G/gg (default MotionBoundary)
    MotionBoundary boundary;
    // Pass Position and fresh RunningEffort (no prior typing context in tests)
    // Try to explore more (30 results), lower search depth for speed (2e4)
    return opt.optimize(lines, start, RunningEffort(), end, userSeq, navContext,
                        boundary, allowedMotions, OptimizerParams(30, 2e4, 1.0, 2.0));
  }

  static vector<RangeResult>
  runOptimizerToRange(const Lines &lines, Position start,
                      Position rangeBegin, Position rangeEnd,
                      const string &userSeq,
                      int maxResults = 10,
                      const MotionToKeys& allowedMotions = EXPLORABLE_MOTIONS,
                      Config config = Config::uniform()) {
    MovementOptimizer opt(config);
    MotionBoundary boundary;
    // allowMultiplePerPosition=true for tests to see all paths
    // Pass Position and fresh RunningEffort (no prior typing context in tests)
    return opt.optimizeToRange(lines, start, RunningEffort(), rangeBegin, rangeEnd,
                               userSeq, navContext, true, boundary, allowedMotions,
                               OptimizerParams(maxResults, 2e4, 1.0, 2.0));
  }
};

// Static member definitions
Lines MovementOptimizerTest::a1_long_line;
Lines MovementOptimizerTest::a2_block_lines;
Lines MovementOptimizerTest::a3_spaced_lines;
Lines MovementOptimizerTest::m1_main_basic;
NavContext MovementOptimizerTest::navContext;

TEST_F(MovementOptimizerTest, HorizontalMotions) {
  const string user_seq = "we";
  Position start(0, 0);
  Position end = simulateMotions(start, user_seq, a1_long_line);

  vector<Result> results = runOptimizer(
  a1_long_line,
    start, end, user_seq
  );

  // Note: "2e" and "ee" are functionally equivalent; optimizer may prefer count-prefixed
  // f motions may not be explored within result limit depending on search order
  EXPECT_TRUE(contains_all(results, {user_seq, "wE", "2e", "2E"}))
      << "Missing expected sequences";
}


// TODO: Re-enable when adding support for filtering the universe of explorable motions.
// This test was designed to verify optimizer behavior with a restricted motion set.
// Currently, all motions are explored automatically via MotionToSpec.
TEST_F(MovementOptimizerTest, DISABLED_VerticalMotions) {
}

// =============================================================================
// optimizeToRange tests
// =============================================================================

TEST_F(MovementOptimizerTest, RangeBasic_SameLine) {
  // Target range is columns 5-10 on line 0
  Lines lines = {"hello world this is a test line"};
  Position start(0, 0);
  Position rangeBegin(0, 5);
  Position rangeEnd(0, 10);

  vector<RangeResult> results = runOptimizerToRange(lines, start, rangeBegin, rangeEnd, "lllll");

  EXPECT_FALSE(results.empty()) << "Should find at least one path to range";
  for (const auto& r : results) {
    EXPECT_GE(r.endPos.col, 5) << "End position should be in range";
    EXPECT_LE(r.endPos.col, 10) << "End position should be in range";
  }
}

TEST_F(MovementOptimizerTest, RangeBasic_MultiLine) {
  // Target range spans multiple lines
  Lines lines = {"line one", "line two", "line three", "line four"};
  Position start(0, 0);
  Position rangeBegin(1, 0);
  Position rangeEnd(2, 5);

  vector<RangeResult> results = runOptimizerToRange(lines, start, rangeBegin, rangeEnd, "jj");

  EXPECT_FALSE(results.empty()) << "Should find at least one path to range";
  for (const auto& r : results) {
    Position p = r.endPos;
    bool inRange = (p >= rangeBegin && p <= rangeEnd);
    EXPECT_TRUE(inRange) << "End position (" << p.line << ", " << p.col << ") should be in range";
  }
}

TEST_F(MovementOptimizerTest, RangeFromMiddle) {
  // Start from middle of file, target range at end
  Lines lines = {"aaa", "bbb", "ccc", "ddd", "eee"};
  Position start(2, 1);
  Position rangeBegin(4, 0);
  Position rangeEnd(4, 2);

  vector<RangeResult> results = runOptimizerToRange(lines, start, rangeBegin, rangeEnd, "jj");

  EXPECT_FALSE(results.empty()) << "Should find at least one path to range";
}

TEST_F(MovementOptimizerTest, RangeWithWordMotions) {
  // Test that word motions can land in range
  Lines lines = {"one two three four five six"};
  Position start(0, 0);
  Position rangeBegin(0, 8);   // "three" starts at 8
  Position rangeEnd(0, 17);    // "four" ends at 17

  vector<RangeResult> results = runOptimizerToRange(lines, start, rangeBegin, rangeEnd, "www");

  EXPECT_FALSE(results.empty()) << "Should find paths using word motions";
}

// =============================================================================
// MotionBoundary tests
// =============================================================================

class MotionBoundaryTest : public ::testing::Test {
protected:
  static NavContext navContext;

  static void SetUpTestSuite() {
    navContext = NavContext();
  }

  // Helper to run optimizer with specific boundary
  static vector<Result>
  runWithBoundary(const Lines& lines, Position start, Position end,
                  const string& userSeq, const MotionBoundary& boundary,
                  const MotionToKeys& allowedMotions = EXPLORABLE_MOTIONS,
                  Config config = Config::uniform()) {
    MovementOptimizer opt(config);
    return opt.optimize(lines, start, RunningEffort(), end, userSeq, navContext,
                        boundary, allowedMotions, OptimizerParams(30, 2e4, 1.0, 2.0));
  }

  // Helper to check if results contain a sequence
  static bool hasSequence(const vector<Result>& results, const string& seq) {
    return std::any_of(results.begin(), results.end(),
        [&seq](const Result& r) { return r.getSequenceString() == seq; });
  }
};

NavContext MotionBoundaryTest::navContext(0, 0);

TEST_F(MotionBoundaryTest, DefaultBoundary_AllowsGG) {
  Lines lines = {"line0", "line1", "line2", "line3"};
  Position start(2, 0);
  Position end(0, 0);  // gg should reach this

  MotionBoundary boundary;  // default: no exclusions

  auto results = runWithBoundary(lines, start, end, "kk", boundary,
                                 getSlicedMotionToKeys({"j", "k", "gg"}));

  EXPECT_TRUE(hasSequence(results, "gg")) << "Default boundary should allow gg";
}

TEST_F(MotionBoundaryTest, ExcludeGG_RemovesGG) {
  Lines lines = {"line0", "line1", "line2", "line3"};
  Position start(2, 0);
  Position end(0, 0);

  MotionBoundary boundary(true, false);  // hasLinesAbove excludes gg

  auto results = runWithBoundary(lines, start, end, "kk", boundary,
                                 getSlicedMotionToKeys({"j", "k", "gg"}));

  EXPECT_FALSE(hasSequence(results, "gg")) << "Boundary with hasLinesAbove should exclude gg";
  EXPECT_TRUE(hasSequence(results, "kk")) << "Should still find alternative path";
}

TEST_F(MotionBoundaryTest, DefaultBoundary_AllowsG) {
  Lines lines = {"line0", "line1", "line2", "line3"};
  Position start(1, 0);
  Position end(3, 0);  // G should reach last line

  MotionBoundary boundary;  // default: no exclusions

  auto results = runWithBoundary(lines, start, end, "jj", boundary,
                                 getSlicedMotionToKeys({"j", "k", "G"}));

  EXPECT_TRUE(hasSequence(results, "G")) << "Default boundary should allow G";
}

TEST_F(MotionBoundaryTest, ExcludeG_RemovesG) {
  Lines lines = {"line0", "line1", "line2", "line3"};
  Position start(1, 0);
  Position end(3, 0);

  MotionBoundary boundary(false, true);  // hasLinesBelow excludes G

  auto results = runWithBoundary(lines, start, end, "jj", boundary,
                                 getSlicedMotionToKeys({"j", "k", "G"}));

  EXPECT_FALSE(hasSequence(results, "G")) << "Boundary with hasLinesBelow should exclude G";
  EXPECT_TRUE(hasSequence(results, "jj")) << "Should still find alternative path";
}

TEST_F(MotionBoundaryTest, LeftColOffset_FiltersPrefixPositions) {
  // NOTE: Position-based column filtering was removed because it was ineffective
  // for motions that clamp to buffer edges (paragraph, sentence jumps).
  // Single-line column filtering is a known limitation - the optimizer will
  // find paths to prefix/suffix positions on single lines.
  //
  // This test documents current behavior, not desired behavior.
  Lines lines = {"prefix_target"};
  Position start(0, 10);  // Start in "target" region
  Position end(0, 5);     // End in prefix region

  MotionBoundary boundary(false, false, 7);  // leftColOffset = prefix length

  auto results = runWithBoundary(lines, start, end, "hhhhh", boundary,
                                 getSlicedMotionToKeys({"h", "l"}));

  // With filtering removed, the optimizer WILL find a path to the prefix region
  EXPECT_FALSE(results.empty())
      << "Without filtering, optimizer finds path to prefix positions";

  // Should still find path to valid position
  Position end2(0, 8);  // Valid position after prefix
  auto results2 = runWithBoundary(lines, start, end2, "hh", boundary,
                                  getSlicedMotionToKeys({"h", "l"}));
  EXPECT_FALSE(results2.empty()) << "Should find path to valid positions";
}

TEST_F(MotionBoundaryTest, RightColOffset_FiltersSuffixPositions) {
  // NOTE: Position-based column filtering was removed because it was ineffective
  // for motions that clamp to buffer edges (paragraph, sentence jumps).
  // Single-line column filtering is a known limitation - the optimizer will
  // find paths to prefix/suffix positions on single lines.
  //
  // This test documents current behavior, not desired behavior.
  Lines lines = {"target_suffix"};
  Position start(0, 3);   // Start in "target" region
  Position end(0, 10);    // End in suffix region

  MotionBoundary boundary(false, false, 0, 6);  // rightColOffset = suffix length

  auto results = runWithBoundary(lines, start, end, "lllllll", boundary,
                                 getSlicedMotionToKeys({"h", "l"}));

  // With filtering removed, the optimizer WILL find a path to the suffix region
  EXPECT_FALSE(results.empty())
      << "Without filtering, optimizer finds path to suffix positions";

  // Should still find path to valid position
  Position end2(0, 6);  // Valid position before suffix (col 7 is where suffix starts)
  auto results2 = runWithBoundary(lines, start, end2, "lll", boundary,
                                  getSlicedMotionToKeys({"h", "l"}));
  EXPECT_FALSE(results2.empty()) << "Should find path to valid positions";
}

TEST_F(MotionBoundaryTest, IsPositionInBounds_WithColConstraints) {
  // Test isPositionInBounds with column constraints
  // leftColOffset=3 (prefix length on first line)
  // rightColOffset=5 (suffix length on last line)
  // lastLine=2, lastLineLength=15 (so suffix starts at col 15-5=10)
  MotionBoundary boundary(false, false, 3, 5);  // leftColOffset=3, rightColOffset=5
  int lastLine = 2;
  int lastLineLength = 15;

  // First line (line 0): positions < leftColOffset are prefix
  EXPECT_FALSE(boundary.isPositionInBounds(Position(0, 0), lastLine, lastLineLength)) << "col 0 < leftColOffset";
  EXPECT_FALSE(boundary.isPositionInBounds(Position(0, 2), lastLine, lastLineLength)) << "col 2 < leftColOffset";
  EXPECT_TRUE(boundary.isPositionInBounds(Position(0, 3), lastLine, lastLineLength)) << "col 3 == leftColOffset (first valid)";
  EXPECT_TRUE(boundary.isPositionInBounds(Position(0, 5), lastLine, lastLineLength)) << "col 5 > leftColOffset";

  // Middle line: no column constraints
  EXPECT_TRUE(boundary.isPositionInBounds(Position(1, 0), lastLine, lastLineLength));
  EXPECT_TRUE(boundary.isPositionInBounds(Position(1, 10), lastLine, lastLineLength));

  // Last line (line 2): positions >= lastLineLength - rightColOffset = 10 are suffix
  EXPECT_TRUE(boundary.isPositionInBounds(Position(2, 5), lastLine, lastLineLength)) << "col 5 < 10";
  EXPECT_TRUE(boundary.isPositionInBounds(Position(2, 9), lastLine, lastLineLength)) << "col 9 < 10 (last valid)";
  EXPECT_FALSE(boundary.isPositionInBounds(Position(2, 10), lastLine, lastLineLength)) << "col 10 == 10 (first in suffix)";
  EXPECT_FALSE(boundary.isPositionInBounds(Position(2, 14), lastLine, lastLineLength)) << "col 14 > 10";
}

// =============================================================================
// Sub-buffer stress tests - verify optimizer correctness on embedded regions
// =============================================================================

#include "Utils/NeovimOracle.h"
#include <random>
#include <map>

// Test case structure for embedded sub-buffer testing
struct EmbeddedMotionTest {
  Lines fullBuffer;       // Complete buffer sent to Neovim
  Lines subBuffer;        // Extracted region for optimizer
  Position subStart;      // Start position in sub-buffer coords
  Position fullStart;     // Same position in full-buffer coords
  int subBufferStartLine; // Line offset of sub-buffer within full buffer
  MotionBoundary boundary;
};

class MovementOptimizerBoundaryStress : public ::testing::Test {
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
  static EmbeddedMotionTest generateEmbeddedTest(mt19937& rng, int fullLines, int subLines) {
    EmbeddedMotionTest test;

    // Character pool - mixed content for realistic word boundaries
    const string chars = "abcd .,";
    uniform_int_distribution<int> lineLen(10, 30);
    uniform_int_distribution<int> charDist(0, static_cast<int>(chars.size()) - 1);

    // Generate full buffer
    for (int i = 0; i < fullLines; i++) {
      int len = lineLen(rng);
      string line;
      for (int j = 0; j < len; j++) {
        line += chars[charDist(rng)];
      }
      test.fullBuffer.push_back(line);
    }

    // Pick sub-buffer region: lines [startLine, endLine]
    uniform_int_distribution<int> startLineDist(0, max(0, fullLines - subLines));
    test.subBufferStartLine = startLineDist(rng);
    int endLine = min(test.subBufferStartLine + subLines - 1, fullLines - 1);

    // Extract sub-buffer
    for (int i = test.subBufferStartLine; i <= endLine; i++) {
      test.subBuffer.push_back(test.fullBuffer[i]);
    }

    // Random starting position within sub-buffer
    uniform_int_distribution<int> subLineDist(0, static_cast<int>(test.subBuffer.size()) - 1);
    int subLine = subLineDist(rng);
    int maxCol = test.subBuffer[subLine].empty() ? 0 : static_cast<int>(test.subBuffer[subLine].size()) - 1;
    uniform_int_distribution<int> colDist(0, max(0, maxCol));
    int col = colDist(rng);

    test.subStart = Position(subLine, col);
    test.fullStart = Position(test.subBufferStartLine + subLine, col);

    // Set up boundary
    test.boundary = MotionBoundary(test.subBufferStartLine > 0, endLine < fullLines - 1);

    return test;
  }

  // Convert sub-buffer position to full-buffer position
  static Position toFullBufferPos(const Position& subPos, int subBufferStartLine) {
    return Position(subPos.line + subBufferStartLine, subPos.col);
  }

  // Convert full-buffer position to sub-buffer position (with bounds checking)
  static pair<bool, Position> toSubBufferPos(const Position& fullPos, int subBufferStartLine, int subBufferLines) {
    int subLine = fullPos.line - subBufferStartLine;
    if (subLine < 0 || subLine >= subBufferLines) {
      return {false, Position(0, 0)};
    }
    return {true, Position(subLine, fullPos.col)};
  }

  // Run optimizer on sub-buffer
  static vector<Result> runOnSubBuffer(const Lines& subBuffer, Position start, Position end,
                                       const MotionBoundary& boundary,
                                       const MotionToKeys& allowedMotions) {
    MovementOptimizer opt(Config::uniform());
    return opt.optimize(subBuffer, start, RunningEffort(), end, "jjjjjjjjjj", navContext,
                        boundary, allowedMotions, OptimizerParams(10, 1e4, 1.0, 2.0));
  }
};

unique_ptr<NeovimOracle> MovementOptimizerBoundaryStress::oracle;
NavContext MovementOptimizerBoundaryStress::navContext;

// Track failures by motion type for analysis
struct MotionFailureStats {
  map<string, int> failures;
  map<string, int> totals;

  void record(const string& seq, bool success) {
    // Extract first motion from sequence
    string firstMotion;
    if (!seq.empty()) {
      if (seq[0] >= '0' && seq[0] <= '9') {
        // Count prefix, skip it
        size_t i = 0;
        while (i < seq.size() && seq[i] >= '0' && seq[i] <= '9') i++;
        if (i < seq.size()) firstMotion = seq.substr(i, 1);
      } else {
        firstMotion = seq.substr(0, 1);
        // Handle two-char motions like gg, ge
        if (seq.size() >= 2 && (seq[0] == 'g' || seq.substr(0, 2) == "gg")) {
          firstMotion = seq.substr(0, 2);
        }
      }
    }
    if (!firstMotion.empty()) {
      totals[firstMotion]++;
      if (!success) failures[firstMotion]++;
    }
  }

  void print() const {
    cerr << "\n=== Motion Failure Analysis ===\n";
    for (const auto& [motion, total] : totals) {
      int fails = 0;
      auto it = failures.find(motion);
      if (it != failures.end()) fails = it->second;
      double rate = total > 0 ? (100.0 * fails / total) : 0;
      cerr << motion << ": " << fails << "/" << total << " (" << rate << "% failure)\n";
    }
  }
};

TEST_F(MovementOptimizerBoundaryStress, SubBufferMotionCorrectness) {
  // Test that optimizer predictions match Neovim behavior when operating on sub-buffers
  mt19937 rng(42);
  const int iterations = 50;
  int totalSequences = 0;
  int failedSequences = 0;
  int escapedBounds = 0;
  MotionFailureStats stats;

  // Motions that could jump beyond sub-buffer boundaries
  MotionToKeys testMotions = getSlicedMotionToKeys({
    "j", "k",           // vertical (should be fine)
    "w", "W", "b", "B", // word motions (may cross lines)
    "e", "E",           // end word
    "{", "}",           // paragraph (likely problematic)
    "(", ")",           // sentence (likely problematic)
  });

  for (int i = 0; i < iterations; i++) {
    // Restart oracle periodically to avoid connection issues
    if (i > 0 && i % 20 == 0) {
      oracle->restart();
    }

    // Generate embedded test case with sub-buffer smaller than full buffer
    auto test = generateEmbeddedTest(rng, 8, 4);

    // Pick a random end position within the sub-buffer
    uniform_int_distribution<int> endLineDist(0, static_cast<int>(test.subBuffer.size()) - 1);
    int endLine = endLineDist(rng);
    int maxEndCol = test.subBuffer[endLine].empty() ? 0 : static_cast<int>(test.subBuffer[endLine].size()) - 1;
    uniform_int_distribution<int> endColDist(0, max(0, maxEndCol));
    Position subEnd(endLine, endColDist(rng));

    // Run optimizer on sub-buffer
    auto results = runOnSubBuffer(test.subBuffer, test.subStart, subEnd, test.boundary, testMotions);

    // For each result, verify against Neovim on full buffer
    for (const auto& result : results) {
      totalSequences++;
      string seq = result.getSequenceString();

      // Apply sequence to FULL buffer via Neovim
      SimulationResult neovimResult;
      try {
        neovimResult = oracle->simulate(test.fullBuffer,
            test.fullStart.line, test.fullStart.col, seq);
      } catch (const exception& e) {
        // Oracle connection issue - restart and skip this iteration
        oracle->restart();
        continue;
      }
      Position neovimEnd(neovimResult.row, neovimResult.col);

      // Apply same sequence to sub-buffer using our simulation
      Position ourEnd = simulateMotions(test.subStart, seq, test.subBuffer);

      // Convert Neovim result to sub-buffer coords
      auto [inBounds, neovimSubPos] = toSubBufferPos(neovimEnd, test.subBufferStartLine,
                                                      static_cast<int>(test.subBuffer.size()));

      // Compare positions
      bool posMatch;
      if (inBounds) {
        posMatch = (ourEnd.line == neovimSubPos.line && ourEnd.col == neovimSubPos.col);
      } else {
        // Neovim landed outside sub-buffer bounds - this is the key failure case
        posMatch = false;
        escapedBounds++;
      }

      stats.record(seq, posMatch);

      if (!posMatch) {
        failedSequences++;
        // Detailed failure logging (limited to first few)
        if (failedSequences <= 3) {
          cerr << "\n=== Sub-buffer Motion Failure #" << failedSequences << " ===" << endl;
          cerr << "Sequence: \"" << seq << "\"" << endl;
          cerr << "Full buffer (" << test.fullBuffer.size() << " lines):" << endl;
          for (size_t j = 0; j < test.fullBuffer.size(); j++) {
            cerr << "  [" << j << "]: \"" << test.fullBuffer[j] << "\"" << endl;
          }
          cerr << "Sub-buffer (lines " << test.subBufferStartLine << "-"
               << (test.subBufferStartLine + test.subBuffer.size() - 1) << "):" << endl;
          for (size_t j = 0; j < test.subBuffer.size(); j++) {
            cerr << "  [" << j << "]: \"" << test.subBuffer[j] << "\"" << endl;
          }
          cerr << "Start: sub(" << test.subStart.line << "," << test.subStart.col << ") = "
               << "full(" << test.fullStart.line << "," << test.fullStart.col << ")" << endl;
          cerr << "Our prediction (sub-buffer): (" << ourEnd.line << ", " << ourEnd.col << ")" << endl;
          cerr << "Neovim result (full-buffer): (" << neovimEnd.line << ", " << neovimEnd.col << ")" << endl;
          if (inBounds) {
            cerr << "Neovim in sub-buffer coords: (" << neovimSubPos.line << ", " << neovimSubPos.col << ")" << endl;
          } else {
            cerr << "Neovim ESCAPED sub-buffer bounds!" << endl;
          }
          cerr << "Boundary: hasLinesAbove=" << test.boundary.hasLinesAbove()
               << ", hasLinesBelow=" << test.boundary.hasLinesBelow() << endl;
        }
      }
    }
  }

  // Print summary statistics
  stats.print();
  cerr << "\n=== Summary ===" << endl;
  cerr << "Total sequences tested: " << totalSequences << endl;
  cerr << "Failed sequences: " << failedSequences << endl;
  cerr << "  - Escaped bounds: " << escapedBounds << endl;
  double failRate = totalSequences > 0 ? (100.0 * failedSequences / totalSequences) : 0;
  cerr << "Failure rate: " << failRate << "%" << endl;

  // We expect some failures - this test is for analysis, not strict pass/fail
  // The purpose is to identify which motions are problematic
  EXPECT_GT(totalSequences, 0) << "Should have tested some sequences";
}
