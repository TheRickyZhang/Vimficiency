// Debug.cpp - Debug utilities and scratch tests for development
//
// This file provides:
// 1. SequenceTracer - Helper class for step-by-step command tracing
// 2. Debug test fixtures for investigating specific failures
// 3. Scratch test space for quick experiments
//
// Usage:
//   - Enable a test by removing DISABLED_ prefix
//   - Run with: ./vimficiency_tests --gtest_filter="DebugTest.*"
//   - Or: ./vimficiency_tests --gtest_filter="NeovimOracleDebug.*"

#include <gtest/gtest.h>

#include "Optimizer/Config.h"
#include "Optimizer/EditOptimizer.h"
#include "Optimizer/BufferIndex.h"
#include "Boundary/EditBoundary.h"
#include "Utils/NeovimOracle.h"
#include "VimCore/VimCore.h"
#include "VimCore/VimEndpointUtils.h"
#include "VimCore/VimMotionUtils.h"
#include "VimCore/SentenceEdgeType.h"
#include "Editor/Motion.h"
#include "Editor/Edit.h"

using namespace std;

// ============================================================================
// SequenceTracer - Helper for step-by-step command tracing
// ============================================================================
//
// Example usage:
//   SequenceTracer tracer(oracle, {"hello", "world"}, 0, 2);
//   tracer.trace("dw");
//   tracer.trace("x");
//   tracer.printSummary();
//
class SequenceTracer {
public:
  SequenceTracer(NeovimOracle* oracle, Lines initialBuffer, int startRow, int startCol)
      : oracle_(oracle), buffer_(std::move(initialBuffer)), row_(startRow), col_(startCol) {
    cerr << "=== Trace Start ===" << endl;
    cerr << "Buffer: " << buffer_ << endl;
    cerr << "Cursor: (" << row_ << ", " << col_ << ")" << endl;
    cerr << endl;
  }

  // Execute a single command and print the result
  void trace(const string& cmd) {
    auto r = oracle_->simulate(buffer_, row_, col_, cmd);
    cerr << "After '" << cmd << "' from (" << row_ << "," << col_ << "):" << endl;
    cerr << "  Buffer: " << r.lines << endl;
    cerr << "  Cursor: (" << r.row << ", " << r.col << ")" << endl;

    buffer_ = r.lines;
    row_ = r.row;
    col_ = r.col;
    commands_.push_back(cmd);
  }

  // Execute full sequence at once and compare
  void traceFullSequence(const string& seq) {
    Lines originalBuffer = buffer_;
    int originalRow = row_;
    int originalCol = col_;

    cerr << endl << "=== Full Sequence: '" << seq << "' ===" << endl;
    auto r = oracle_->simulate(originalBuffer, originalRow, originalCol, seq);
    cerr << "Result: " << r.lines << endl;
    cerr << "Cursor: (" << r.row << ", " << r.col << ")" << endl;
    cerr << "Flattened: '" << r.lines.flatten() << "'" << endl;
  }

  // Print sequence bytes for debugging encoding issues
  static void printSequenceBytes(const string& seq) {
    cerr << "Sequence: '" << seq << "' (len=" << seq.size() << ")" << endl;
    cerr << "Bytes: ";
    for (char c : seq) cerr << static_cast<int>(static_cast<unsigned char>(c)) << " ";
    cerr << endl;
  }

  // Print final state
  void printSummary() {
    cerr << endl << "=== Trace Summary ===" << endl;
    cerr << "Commands: ";
    for (const auto& cmd : commands_) cerr << cmd << " ";
    cerr << endl;
    cerr << "Final buffer: " << buffer_ << endl;
    cerr << "Final cursor: (" << row_ << ", " << col_ << ")" << endl;
    cerr << "Flattened: '" << buffer_.flatten() << "'" << endl;
  }

  // Getters
  const Lines& buffer() const { return buffer_; }
  int row() const { return row_; }
  int col() const { return col_; }
  string flattened() const { return buffer_.flatten(); }

private:
  NeovimOracle* oracle_;
  Lines buffer_;
  int row_;
  int col_;
  vector<string> commands_;
};

// ============================================================================
// DebugTest - Basic scratch test fixture
// ============================================================================

class DebugTest : public ::testing::Test {
protected:
  Config config = Config::uniform();

  EditOptimizer makeOptimizer() {
    return EditOptimizer(config, OptimizerParams(30, 1e5, 1.0, 2.0));
  }

  // Create boundary for full buffer deletion (no constraints)
  EditBoundary makeFullBufferBoundary(const Lines& source) {
    int lastLine = static_cast<int>(source.size()) - 1;
    int lastCol = source[lastLine].empty() ? 0 : static_cast<int>(source[lastLine].size()) - 1;
    return EditBoundary(source, Position(0, 0), Position(lastLine, lastCol));
  }
};

TEST_F(DebugTest, Placeholder) {
  EXPECT_TRUE(true);
}

// ============================================================================
// NeovimOracleDebug - Debug tests with Neovim oracle
// ============================================================================

class NeovimOracleDebug : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    oracle_ = std::make_unique<NeovimOracle>();
  }
  static void TearDownTestSuite() {
    oracle_.reset();
  }
  static std::unique_ptr<NeovimOracle> oracle_;

  // Convenience method to create tracer
  SequenceTracer makeTracer(Lines buffer, int row, int col) {
    return SequenceTracer(oracle_.get(), std::move(buffer), row, col);
  }
};

std::unique_ptr<NeovimOracle> NeovimOracleDebug::oracle_;

// ============================================================================
// Example Debug Test Template
// ============================================================================
//
// Copy this template when investigating a new failure:
//
// TEST_F(NeovimOracleDebug, DISABLED_InvestigateFailure) {
//   // Document the failure:
//   // FAIL iter=X pos=[Y,Z] seq='...'
//   // Buffer: ...
//   // Expected: '...'
//   // Got: '...'
//
//   auto tracer = makeTracer({"line1", "line2"}, 0, 0);
//
//   // Trace step by step
//   tracer.trace("cmd1");
//   tracer.trace("cmd2");
//
//   // Also test full sequence
//   tracer.traceFullSequence("cmd1cmd2");
//
//   tracer.printSummary();
//   cerr << "Expected: '...'" << endl;
// }
// Once a test is no longer needed, you can move it to Misc/OldDebugTest.

// ============================================================================
// Begin Debug Tests
// ============================================================================

