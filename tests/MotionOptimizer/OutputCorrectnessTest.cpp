// tests/MotionOptimizer/OutputCorrectnessTest.cpp
//
// Random/stress tests for MotionOptimizer output correctness.
// These tests use randomly generated buffers and verify results against Neovim.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="MotionOptimizerOutputCorrectness.*"

#include <gtest/gtest.h>

#include "Interpreter/MotionInterpreter.h"
#include "Types/NavContext.h"
#include "Keyboard/Config.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"
#include "Boundary/MotionBoundary.h"
#include "Effort/RunningEffort.h"
#include "Types/Lines.h"
#include "Utils/NeovimOracle.h"
#include "Utils/RandomGeneration.h"

#include <map>
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
  MotionBoundary boundary;
};

class MotionOptimizerOutputCorrectness : public ::testing::Test {
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
    test.boundary = MotionBoundary(test.fullBuffer, firstPos, endPos);

    return test;
  }

  // Convert sub-buffer position to full-buffer position
  static CursorPos toFullBufferPos(const CursorPos& subPos, int subBufferStartLine) {
    return CursorPos(subPos.line + subBufferStartLine, subPos.col);
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
  static vector<Result> runOnSubBuffer(const Lines& subBuffer, CursorPos start, CursorPos end,
                                       const MotionBoundary& boundary) {
    MotionOptimizer opt(Config::uniform());
    return opt.optimize(subBuffer, start, end, {},
                        "jjjjjjjjjj", boundary, navContext).getResults();
  }
};

unique_ptr<NeovimOracle> MotionOptimizerOutputCorrectness::oracle;
NavContext MotionOptimizerOutputCorrectness::navContext;

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

// =============================================================================
// Sub-buffer stress tests - verify optimizer correctness on embedded regions
// =============================================================================

TEST_F(MotionOptimizerOutputCorrectness, SubBufferMotionCorrectness) {
  // Test that optimizer predictions match Neovim behavior when operating on sub-buffers
  RandomGen::seed(42);
  const int iterations = 50;
  int totalSequences = 0;
  int failedSequences = 0;
  int escapedBounds = 0;
  MotionFailureStats stats;

  for (int i = 0; i < iterations; i++) {
    // Generate embedded test case with sub-buffer smaller than full buffer
    auto test = generateEmbeddedTest(8, 4);

    // Pick a random end position within the sub-buffer
    int endLine = RandomGen::range(0, static_cast<int>(test.subBuffer.size()) - 1);
    int maxEndCol = test.subBuffer[endLine].empty() ? 0 : static_cast<int>(test.subBuffer[endLine].size()) - 1;
    CursorPos subEnd(endLine, RandomGen::range(0, max(0, maxEndCol)));

    // Run optimizer on sub-buffer
    auto results = runOnSubBuffer(test.subBuffer, test.subStart, subEnd, test.boundary);

    // For each result, verify against Neovim on full buffer
    for (const auto& result : results) {
      totalSequences++;
      const auto& seq = result.getSequence();

      // Apply sequence to FULL buffer via Neovim
      SimulationResult neovimResult;
      try {
        neovimResult = oracle->simulate(test.fullBuffer,
            test.fullStart.line, test.fullStart.col, seq.str());
      } catch (const exception& e) {
        // Oracle connection issue - restart and skip this iteration
        oracle->restart();
        continue;
      }
      CursorPos neovimEnd(neovimResult.row, neovimResult.col);

      // Apply same sequence to sub-buffer using our simulation
      CursorPos ourEnd = simulateMotions(test.subStart, seq.view(), test.subBuffer);

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

      stats.record(seq.str(), posMatch);

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

  EXPECT_GT(totalSequences, 0) << "Should have tested some sequences";
  EXPECT_EQ(failedSequences, 0) << failedSequences << " sequences produced incorrect results";

  // Print summary statistics only on failure
  if (failedSequences > 0) {
    stats.print();
    cerr << "\n=== Summary ===" << endl;
    cerr << "Total sequences tested: " << totalSequences << endl;
    cerr << "Failed sequences: " << failedSequences << endl;
    cerr << "  - Escaped bounds: " << escapedBounds << endl;
    double failRate = totalSequences > 0 ? (100.0 * failedSequences / totalSequences) : 0;
    cerr << "Failure rate: " << failRate << "%" << endl;
  }
}
