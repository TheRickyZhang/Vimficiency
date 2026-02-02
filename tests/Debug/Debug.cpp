// tests/Debug/Debug.cpp
//
// Debug utilities and scratch tests for development.
// Enable a test by removing DISABLED_ prefix.
//
// Run: ./build/tests/vimficiency_debug --gtest_filter="DebugTest.*"
//   - Or: ./vimficiency_tests --gtest_filter="NeovimOracleDebug.*"

#include <gtest/gtest.h>

#include "Optimizer/Config.h"
#include "Optimizer/EditOptimizer/EditOptimizer.h"
#include "Optimizer/BufferIndex.h"
#include "Boundary/EditBoundary.h"
#include "Utils/NeovimOracle.h"
#include "VimCore/VimCore.h"
#include "VimCore/VimEditUtils.h"
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
  EditOptimizerParams params = EditOptimizerParams{}.withMaxNodesExplored(100000);

  EditOptimizer makeOptimizer() {
    return EditOptimizer(config);
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

TEST_F(NeovimOracleDebug, DISABLED_InvestigateEmbeddedFailures) {
  cerr << "=== Embedded Failure 1: dbdddedgehXXXx at [2,0] ===" << endl;
  cerr << "Understanding the test structure:" << endl;
  {
    // From test output:
    // FullBuffer: acffce / adf / ,e / .fe bd
    // EditRegion: cffce / adf / ,e / .fe
    // So prefix = "a", suffix = "bd"
    // editPos=[2,0] means line 2, col 0 of EditRegion = " ,e" at col 0 (the space)
    // bufferPos=[2,0] is the same position in FullBuffer

    Lines fullBuffer = {"acffce", "adf", " ,e", ".fe bd"};
    Lines editRegion = {"cffce", "adf", " ,e", ".fe "};

    cerr << "FullBuffer: " << fullBuffer << endl;
    cerr << "EditRegion: " << editRegion << endl;
    cerr << "Prefix: 'a', Suffix: 'bd'" << endl;
    cerr << "Starting at editPos=[2,0] which is bufferPos=[2,0]" << endl;
    cerr << endl;

    // Sequence: dbdddedgehXXXx = db + dd + de + dge + h + X + X + X + x
    cerr << "=== Full sequence trace: db + dd + de + dge + h + X + X + X + x ===" << endl;
    auto tracer = makeTracer(fullBuffer, 2, 0);
    tracer.trace("db");
    tracer.trace("dd");
    tracer.trace("de");
    tracer.trace("dge");
    tracer.trace("h");
    tracer.trace("X");
    tracer.trace("X");
    tracer.trace("X");
    tracer.trace("x");
    tracer.printSummary();

    // Also test full sequence at once
    cerr << endl << "Full sequence result:" << endl;
    auto r = oracle_->simulate(fullBuffer, 2, 0, "dbdddedgehXXXx");
    cerr << "  Result: '" << r.lines.flatten() << "' cursor=[" << r.row << "," << r.col << "]" << endl;

    cerr << endl << "Expected final: 'abd' (just prefix + suffix)" << endl;

    // Key insight: The optimizer works on effectiveLines which = FullBuffer
    // But the deletion sequence doesn't actually delete line 0 "acffce"!
  }

  cerr << endl << "=== Embedded Failure 2: dddbdbJXXdge at [1,3] ===" << endl;
  {
    // From test output:
    // FullBuffer: ccbbfd , / a cfd / .  c
    // EditRegion: bfd , / a cfd / .
    // So prefix = "ccb", suffix = " c"

    Lines fullBuffer = {"ccbbfd ,", "a cfd", ".  c"};
    Lines editRegion = {"bfd ,", "a cfd", ".  "};

    cerr << "FullBuffer: " << fullBuffer << endl;
    cerr << "EditRegion: " << editRegion << endl;
    cerr << "Prefix: 'ccb', Suffix: ' c'" << endl;
    cerr << "Starting at editPos=[1,3] which is bufferPos=[1,3]" << endl;
    cerr << endl;

    // Sequence: dddbdbJXXdge = dd + db + db + J + X + X + dge
    cerr << "=== Full sequence trace: dd + db + db + J + X + X + dge ===" << endl;
    auto tracer = makeTracer(fullBuffer, 1, 3);
    tracer.trace("dd");
    tracer.trace("db");
    tracer.trace("db");
    tracer.trace("J");
    tracer.trace("X");
    tracer.trace("X");
    tracer.trace("dge");
    tracer.printSummary();

    // Also test full sequence at once
    cerr << endl << "Full sequence result:" << endl;
    auto r = oracle_->simulate(fullBuffer, 1, 3, "dddbdbJXXdge");
    cerr << "  Result: '" << r.lines.flatten() << "' cursor=[" << r.row << "," << r.col << "]" << endl;

    cerr << endl << "Expected final: 'ccbc' (prefix 'ccb' + suffix 'c')" << endl;
  }

  cerr << endl << "=== Detailed db analysis ===" << endl;
  {
    // Test db cursor position after deletion
    Lines test1 = {"acffce", "adf", " ,e", ".fe bd"};
    cerr << "Testing db from [2,0] on: " << test1 << endl;

    auto r1 = oracle_->simulate(test1, 2, 0, "db");
    cerr << "After db: '" << r1.lines.flatten() << "' cursor=[" << r1.row << "," << r1.col << "]" << endl;

    // What does our VimCore think?
    cerr << endl << "Our VimCore simulation:" << endl;
    Lines ourLines = test1;
    Position ourPos(2, 0);

    // Compute endpoint for db
    Position endpoint = VimCore::motionWordEndpoint(ourPos, ourLines, false, EdgeType::WordEdge, false, true, 0, false, /*lineBounded=*/false);
    cerr << "  motion endpoint: [" << endpoint.line << "," << endpoint.col << "]" << endl;

    // Compute range for db (exclusive at cursor)
    Range range;
    int cursorContentCol = ourPos.col;
    if (cursorContentCol > 0) {
      range = Range(endpoint, Position(ourPos.line, ourPos.col - 1));
    } else if (endpoint.line < ourPos.line) {
      int prevLine = ourPos.line - 1;
      range = Range(endpoint, Position(prevLine, ourLines[prevLine].lastCol()));
    }
    cerr << "  deletion range: [" << range.first.line << "," << range.first.col
         << "] to [" << range.last.line << "," << range.last.col << "]" << endl;

    // Apply deletion
    VimCore::deleteRange(ourLines, range, ourPos, Mode::Normal);
    cerr << "  after deletion: '" << ourLines.flatten() << "' cursor=[" << ourPos.line << "," << ourPos.col << "]" << endl;

    if (ourPos.line != r1.row || ourPos.col != r1.col) {
      cerr << "  *** CURSOR MISMATCH! Neovim=[" << r1.row << "," << r1.col << "] vs Ours=[" << ourPos.line << "," << ourPos.col << "]" << endl;
    }
  }

  cerr << endl << "=== More db cursor tests ===" << endl;
  {
    // Test various db scenarios to understand cursor placement
    cerr << "Case 1: db from line start to previous line" << endl;
    {
      Lines lines = {"abc", "def", "ghi"};
      auto r = oracle_->simulate(lines, 2, 0, "db");
      cerr << "  'abc'/'def'/'ghi' db at [2,0]: cursor=[" << r.row << "," << r.col << "] buf='" << r.lines.flatten() << "'" << endl;
    }

    cerr << "Case 2: db from line start with leading space" << endl;
    {
      Lines lines = {"abc", "def", " ghi"};
      auto r = oracle_->simulate(lines, 2, 0, "db");
      cerr << "  'abc'/'def'/' ghi' db at [2,0]: cursor=[" << r.row << "," << r.col << "] buf='" << r.lines.flatten() << "'" << endl;
    }

    cerr << "Case 3: db from col 1 (not line start)" << endl;
    {
      Lines lines = {"abc", "def", "ghi"};
      auto r = oracle_->simulate(lines, 2, 1, "db");
      cerr << "  'abc'/'def'/'ghi' db at [2,1]: cursor=[" << r.row << "," << r.col << "] buf='" << r.lines.flatten() << "'" << endl;
    }

    cerr << "Case 4: db within same line" << endl;
    {
      Lines lines = {"abc def"};
      auto r = oracle_->simulate(lines, 0, 4, "db");
      cerr << "  'abc def' db at [0,4]: cursor=[" << r.row << "," << r.col << "] buf='" << r.lines.flatten() << "'" << endl;
    }

    cerr << "Case 5: db from whitespace at line start" << endl;
    {
      Lines lines = {"abc", "def", "  ghi"};
      auto r = oracle_->simulate(lines, 2, 0, "db");
      cerr << "  'abc'/'def'/'  ghi' db at [2,0]: cursor=[" << r.row << "," << r.col << "] buf='" << r.lines.flatten() << "'" << endl;
    }

    cerr << "Case 6: db from whitespace (col 1)" << endl;
    {
      Lines lines = {"abc", "def", "  ghi"};
      auto r = oracle_->simulate(lines, 2, 1, "db");
      cerr << "  'abc'/'def'/'  ghi' db at [2,1]: cursor=[" << r.row << "," << r.col << "] buf='" << r.lines.flatten() << "'" << endl;
    }
  }

  cerr << endl << "=== Verify cursor placement pattern ===" << endl;
  {
    // Pattern: when db at col 0 (on whitespace) crosses lines,
    // cursor col = first non-blank of original line
    cerr << "Testing db at col 0 with leading whitespace:" << endl;

    // 3 leading spaces
    Lines lines3 = {"abc", "def", "   ghi"};
    auto r3 = oracle_->simulate(lines3, 2, 0, "db");
    cerr << "  '   ghi' db at [2,0]: cursor=[" << r3.row << "," << r3.col << "]";
    cerr << " (expected col=3 for first non-blank)" << endl;

    // tab then chars
    Lines linesTab = {"abc", "def", "\tghi"};
    auto rTab = oracle_->simulate(linesTab, 2, 0, "db");
    cerr << "  '\\tghi' db at [2,0]: cursor=[" << rTab.row << "," << rTab.col << "]";
    cerr << " (expected col=1 for first non-blank)" << endl;

    // Does dB have same behavior?
    cerr << endl << "Testing dB at col 0 with leading whitespace:" << endl;
    Lines linesBig = {"abc", "def", " ghi"};
    auto rBig = oracle_->simulate(linesBig, 2, 0, "dB");
    cerr << "  ' ghi' dB at [2,0]: cursor=[" << rBig.row << "," << rBig.col << "]" << endl;

    // Does it only apply when cursor was at col 0?
    cerr << endl << "When cursor NOT at col 0:" << endl;
    Lines lines2 = {"abc", "def", " ghi"};
    auto r2 = oracle_->simulate(lines2, 2, 1, "db");  // cursor at col 1 (on 'g')
    cerr << "  ' ghi' db at [2,1]: cursor=[" << r2.row << "," << r2.col << "]" << endl;
  }

  cerr << endl << "=== Detailed analysis of failure 2 ===" << endl;
  {
    // After dd+db+db+J+X+X, buffer is "ccbbfc" at [0,5]
    // Then dge from [0,5] deletes everything
    // The optimizer shouldn't have allowed dge that deletes into prefix "ccb"

    cerr << "Testing dge boundary behavior:" << endl;
    Lines test = {"ccbbfc"};  // This is what buffer looks like after prior deletions
    cerr << "Buffer: '" << test[0] << "' (len=6)" << endl;
    cerr << "Prefix='ccb' (leftColOffset=3), so edit content is 'bfc' at cols 3-5" << endl;

    // Test dge from different positions
    auto r5 = oracle_->simulate(test, 0, 5, "dge");
    cerr << "  dge at [0,5]: '" << r5.lines.flatten() << "' cursor=[" << r5.row << "," << r5.col << "]" << endl;

    auto r4 = oracle_->simulate(test, 0, 4, "dge");
    cerr << "  dge at [0,4]: '" << r4.lines.flatten() << "' cursor=[" << r4.row << "," << r4.col << "]" << endl;

    auto r3 = oracle_->simulate(test, 0, 3, "dge");
    cerr << "  dge at [0,3]: '" << r3.lines.flatten() << "' cursor=[" << r3.row << "," << r3.col << "]" << endl;

    // What does ge motion return?
    cerr << endl << "ge motion (no delete):" << endl;
    auto ge5 = oracle_->simulate(test, 0, 5, "ge");
    cerr << "  ge at [0,5]: cursor=[" << ge5.row << "," << ge5.col << "]" << endl;

    auto ge3 = oracle_->simulate(test, 0, 3, "ge");
    cerr << "  ge at [0,3]: cursor=[" << ge3.row << "," << ge3.col << "]" << endl;

    // Test our VimCore endpoint
    cerr << endl << "Our motionWordEndpoint for ge (backward, NextEdge):" << endl;
    Position ep5 = VimCore::motionWordEndpoint(Position(0,5), test, false, EdgeType::NextEdge, false, true, 3, false, /*lineBounded=*/false);
    cerr << "  from [0,5] with leftColOffset=3: ";
    if (ep5 == POSITION_OUTSIDE_BOUNDARY) cerr << "OUTSIDE_BOUNDARY" << endl;
    else cerr << "[" << ep5.line << "," << ep5.col << "]" << endl;

    Position ep3 = VimCore::motionWordEndpoint(Position(0,3), test, false, EdgeType::NextEdge, false, true, 3, false, /*lineBounded=*/false);
    cerr << "  from [0,3] with leftColOffset=3: ";
    if (ep3 == POSITION_OUTSIDE_BOUNDARY) cerr << "OUTSIDE_BOUNDARY" << endl;
    else cerr << "[" << ep3.line << "," << ep3.col << "]" << endl;
  }

  cerr << endl << "=== Trace optimizer path for failure 2 ===" << endl;
  {
    // Trace what the optimizer sees (effectiveLines, not fullBuffer)
    // EditRegion: bfd , / a cfd / .
    // Prefix: ccb, Suffix:  c
    // effectiveLines: ccbbfd , / a cfd / .   c

    Lines effLines = {"ccbbfd ,", "a cfd", ".   c"};  // Note: suffix is " c", so ".  " + " c" = ".   c"
    cerr << "effectiveLines: " << effLines << endl;
    cerr << "editPos [1,3] = effPos [1,3] (on 'f' in 'a cfd')" << endl;

    auto tracer = makeTracer(effLines, 1, 3);
    tracer.trace("dd");
    tracer.trace("db");
    tracer.trace("db");
    tracer.trace("J");
    tracer.printSummary();

    // What are contentStart/contentEnd at this point?
    cerr << endl << "After J, buffer is: '" << tracer.buffer().flatten() << "'" << endl;
    cerr << "Cursor at: [" << tracer.row() << "," << tracer.col() << "]" << endl;

    int leftColOffset = 3;  // prefix "ccb"
    int rightColOffset = 2; // suffix " c"
    int lineLen = tracer.buffer()[0].size();
    int contentStart = leftColOffset;
    int contentEnd = lineLen - rightColOffset;
    cerr << "lineLen=" << lineLen << ", contentStart=" << contentStart << ", contentEnd=" << contentEnd << endl;
    cerr << "cursor.col=" << tracer.col() << " > contentEnd=" << contentEnd << "? " << (tracer.col() > contentEnd ? "YES (in suffix)" : "NO") << endl;
  }

  cerr << endl << "=== Debug dge exploration ===" << endl;
  {
    // After dd+db+db+J+X+X, buffer is "ccbbfc" at [0,5]
    Lines buf = {"ccbbfc"};
    Position cursor(0, 5);
    int leftColOffset = 3;
    int rightColOffset = 1;

    cerr << "Buffer: '" << buf[0] << "' len=" << buf[0].size() << endl;
    cerr << "Cursor: [0,5], leftColOffset=" << leftColOffset << ", rightColOffset=" << rightColOffset << endl;

    // Check suffix region
    int lineLen = buf[0].size();
    bool inSuffix = (cursor.col + rightColOffset >= lineLen);
    cerr << "In suffix? " << cursor.col << " + " << rightColOffset << " >= " << lineLen << " = " << (inSuffix ? "YES" : "NO") << endl;

    // Check what motionWordEndpoint returns for dge (backward, NextEdge)
    Position endpoint = VimCore::motionWordEndpoint(cursor, buf, false, EdgeType::NextEdge, false, true, leftColOffset, false, /*lineBounded=*/false);
    cerr << "ge endpoint from [0,5] with leftColOffset=3: ";
    if (endpoint == POSITION_OUTSIDE_BOUNDARY) cerr << "OUTSIDE_BOUNDARY" << endl;
    else cerr << "[" << endpoint.line << "," << endpoint.col << "]" << endl;

    // Check inBoundaryRegion - this should match what the optimizer does
    // Simulate the optimizer's check
    bool inBoundary = false;
    if (cursor.line == buf.lastLine() && rightColOffset > 0 &&
        cursor.col + rightColOffset >= static_cast<int>(buf[cursor.line].size())) {
      inBoundary = true;
    }
    cerr << "inBoundary (suffix check): " << (inBoundary ? "YES" : "NO") << endl;

    // The actual inBoundaryRegion function uses a slightly different check
    // It checks pos.col >= lineLen - rightColOffset
    bool inBound2 = (cursor.col >= lineLen - rightColOffset);
    cerr << "inBoundaryRegion formula: " << cursor.col << " >= " << (lineLen - rightColOffset) << " = " << (inBound2 ? "YES" : "NO") << endl;
  }

  cerr << endl << "=== Test optimizer with EXACT test case ===" << endl;
  {
    Lines fullBuffer = {"ccbbfd ,", "a cfd", ".  c"};
    Lines editRegion = {"bfd ,", "a cfd", ".  "};

    EditBoundary boundary(fullBuffer, Position(0, 3), Position(2, 2));
    cerr << "Prefix: '" << boundary.prefix() << "', Suffix: '" << boundary.suffix() << "'" << endl;

    Config config = Config::uniform();
    EditOptimizerParams params = EditOptimizerParams{}.withMaxNodesExplored(50000);
    EditOptimizer opt(config);
    EditResult res = opt.optimizeEdit(editRegion, {""}, boundary, params);

    int flatIdx = 5 + 3;
    if (flatIdx < static_cast<int>(res.typeAllResults.size())) {
      const auto& r = res.typeAllResults[flatIdx];
      if (r.isValid()) {
        cerr << "Result: '" << r.getSequenceString() << "'" << endl;
        auto nvim = oracle_->simulate(fullBuffer, 1, 3, r.getSequenceString());
        cerr << "Neovim: '" << nvim.lines.flatten() << "', Expected: 'ccbc'" << endl;
      } else {
        cerr << "INVALID" << endl;
      }
    }
  }

  cerr << endl << "=== Analysis ===" << endl;
  cerr << "When db at col 0 crosses lines, cursor goes to first non-blank of current line." << endl;
}

TEST_F(NeovimOracleDebug, DISABLED_InvestigateSentenceMotion) {
  cerr << "=== Understanding d( sentence motion ===" << endl;

  // From the failing test
  Lines source = {"d..,b,b", "eafecf  ", ".f ,b,f"};
  cerr << "Source:" << endl;
  for (size_t i = 0; i < source.size(); i++) {
    cerr << "  [" << i << "]: '" << source[i] << "' len=" << source[i].size() << endl;
  }

  // Test d( at various positions
  cerr << "\nd( from different positions:" << endl;
  for (int col = 0; col <= 7; col++) {
    auto r = oracle_->simulate(source, 1, col, "d(");
    cerr << "  d( at [1," << col << "]: '" << r.lines.flatten() << "' cursor=[" << r.row << "," << r.col << "]" << endl;
  }

  // What about the ( motion alone (without delete)?
  cerr << "\n( motion (cursor only) from different positions:" << endl;
  for (int col = 0; col <= 7; col++) {
    auto r = oracle_->simulate(source, 1, col, "(");
    cerr << "  ( at [1," << col << "]: cursor=[" << r.row << "," << r.col << "]" << endl;
  }

  // Test simpler case
  cerr << "\nSimpler case - 'Hello. World' on two lines:" << endl;
  Lines simple = {"Hello.", "World here."};
  cerr << "Source: '" << simple[0] << "' / '" << simple[1] << "'" << endl;

  auto r1 = oracle_->simulate(simple, 1, 0, "d(");
  cerr << "  d( at [1,0]: '" << r1.lines.flatten() << "' cursor=[" << r1.row << "," << r1.col << "]" << endl;

  auto r2 = oracle_->simulate(simple, 1, 5, "d(");
  cerr << "  d( at [1,5]: '" << r2.lines.flatten() << "' cursor=[" << r2.row << "," << r2.col << "]" << endl;

  // What about d) ?
  cerr << "\nd) from different positions on source buffer:" << endl;
  for (int line = 0; line < 3; line++) {
    auto r = oracle_->simulate(source, line, 0, "d)");
    cerr << "  d) at [" << line << ",0]: '" << r.lines.flatten() << "' cursor=[" << r.row << "," << r.col << "]" << endl;
  }

  // Is d( linewise when crossing lines?
  cerr << "\nChecking if d( is linewise:" << endl;
  Lines test = {"abc", "def", "ghi"};
  auto rtest = oracle_->simulate(test, 1, 2, "d(");
  cerr << "  'abc'/'def'/'ghi' d( at [1,2]: '" << rtest.lines.flatten() << "' cursor=[" << rtest.row << "," << rtest.col << "]" << endl;
  // If linewise, would delete entire line 0 and line 1
  // If characterwise, would delete from [0,0] to [1,1] (exclusive of [1,2])
}

TEST_F(NeovimOracleDebug, DISABLED_InvestigateRemainingFailures) {
  // These failures have been fixed:
  // - d( sentence motion when cursor on trailing whitespace at EOL
  // - daw text object when trailing whitespace ends at EOL
  cerr << "=== Failure 1: dddawD at [0,2] ===" << endl;
  {
    Lines source = {"ede.", "ceddc ", " dfdcad"};
    cerr << "Source: '" << source[0] << "' / '" << source[1] << "' / '" << source[2] << "'" << endl;
    cerr << "Line lengths: " << source[0].size() << ", " << source[1].size() << ", " << source[2].size() << endl;

    auto tracer = makeTracer(source, 0, 2);
    tracer.trace("dd");
    tracer.trace("daw");
    tracer.trace("D");
    tracer.printSummary();
    cerr << "Expected: empty" << endl;

    // Now verify what our VimCore thinks
    cerr << endl << "=== Our VimCore simulation ===" << endl;
    Lines testLines = source;
    Position pos(0, 2);

    // Simulate dd
    cerr << "Before dd: " << testLines << " pos=(" << pos.line << "," << pos.col << ")" << endl;
    VimCore::deleteRangeLinewise(testLines, LineRange(0, 0), pos);
    cerr << "After dd: " << testLines << " pos=(" << pos.line << "," << pos.col << ")" << endl;

    // Simulate daw - first check what textObjectRange returns
    cerr << endl << "textObjectRange for daw at (" << pos.line << "," << pos.col << "):" << endl;
    Range dawRange = VimCore::textObjectRange(pos, testLines, false, false, 0, 0, false, false);
    cerr << "  Range: [" << dawRange.first.line << "," << dawRange.first.col << "] to ["
         << dawRange.last.line << "," << dawRange.last.col << "]" << endl;

    // Apply the deletion
    VimCore::deleteRange(testLines, dawRange, pos, Mode::Normal);
    cerr << "After daw: " << testLines << " pos=(" << pos.line << "," << pos.col << ")" << endl;
    cerr << "  Line count: " << testLines.size() << ", Line 0 empty: " << testLines[0].empty() << endl;
  }

  cerr << endl << "=== Failure 2: dawddD at [1,0] ===" << endl;
  {
    Lines source = {"ede.", "ceddc ", " dfdcad"};
    cerr << "Source: '" << source[0] << "' / '" << source[1] << "' / '" << source[2] << "'" << endl;

    auto tracer = makeTracer(source, 1, 0);
    tracer.trace("daw");
    tracer.trace("dd");
    tracer.trace("D");
    tracer.printSummary();
    cerr << "Expected: empty" << endl;
  }

  cerr << endl << "=== Failure 2b: dawddD at [1,2] ===" << endl;
  {
    Lines source = {"ede.", "ceddc ", " dfdcad"};
    cerr << "Source: '" << source[0] << "' / '" << source[1] << "' / '" << source[2] << "'" << endl;

    auto tracer = makeTracer(source, 1, 2);
    tracer.trace("daw");
    tracer.trace("dd");
    tracer.trace("D");
    tracer.printSummary();
    cerr << "Expected: empty" << endl;
  }

  cerr << endl << "=== Understanding daw on single word line with trailing space ===" << endl;
  {
    Lines source = {"abc", "ceddc ", "def"};
    cerr << "Source: 'abc' / 'ceddc ' / 'def'" << endl;

    // daw at start of "ceddc " - should delete the word and trailing space
    auto r1 = oracle_->simulate(source, 1, 0, "daw");
    cerr << "  daw at [1,0]: '" << r1.lines.flatten() << "' cursor=[" << r1.row << "," << r1.col << "]" << endl;

    // What about when it's the only content on line?
    Lines source2 = {"abc", "word ", "def"};
    auto r2 = oracle_->simulate(source2, 1, 0, "daw");
    cerr << "  daw on 'abc'/'word '/'def' at [1,0]: '" << r2.lines.flatten() << "' cursor=[" << r2.row << "," << r2.col << "]" << endl;

    // Does daw join lines when deleting entire line content?
    Lines source3 = {"abc", "word", "def"};
    auto r3 = oracle_->simulate(source3, 1, 0, "daw");
    cerr << "  daw on 'abc'/'word'/'def' at [1,0]: '" << r3.lines.flatten() << "' cursor=[" << r3.row << "," << r3.col << "]" << endl;
  }

  cerr << endl << "=== dw behavior investigation ===" << endl;
  {
    // What does dw do at end of line?
    Lines source = {"b,cf", " f.ef,", "c .ecee"};
    auto r1 = oracle_->simulate(source, 0, 2, "dw");
    cerr << "  'b,cf'/... dw at [0,2] ('c'): '" << r1.lines.flatten() << "' cursor=[" << r1.row << "," << r1.col << "]" << endl;

    // dw at very end of line
    auto r2 = oracle_->simulate(source, 0, 3, "dw");
    cerr << "  'b,cf'/... dw at [0,3] ('f'): '" << r2.lines.flatten() << "' cursor=[" << r2.row << "," << r2.col << "]" << endl;

    // Compare: does dw cross lines?
    Lines source2 = {"abc", "def"};
    auto r3 = oracle_->simulate(source2, 0, 2, "dw");
    cerr << "  'abc'/'def' dw at [0,2] ('c'): '" << r3.lines.flatten() << "' cursor=[" << r3.row << "," << r3.col << "]" << endl;

    // What about when there's whitespace at next line start?
    Lines source3 = {"abc", " def"};
    auto r4 = oracle_->simulate(source3, 0, 2, "dw");
    cerr << "  'abc'/' def' dw at [0,2] ('c'): '" << r4.lines.flatten() << "' cursor=[" << r4.row << "," << r4.col << "]" << endl;

    // Key test: trailing space on same line vs newline
    Lines source4 = {"abc ", "def"};  // trailing space
    auto r5 = oracle_->simulate(source4, 0, 2, "dw");
    cerr << "  'abc '/'def' dw at [0,2] ('c'): '" << r5.lines.flatten() << "' cursor=[" << r5.row << "," << r5.col << "]" << endl;

    Lines source5 = {"abc  ", "def"};  // two trailing spaces
    auto r6 = oracle_->simulate(source5, 0, 2, "dw");
    cerr << "  'abc  '/'def' dw at [0,2] ('c'): '" << r6.lines.flatten() << "' cursor=[" << r6.row << "," << r6.col << "]" << endl;

    // dw in middle of line with word after
    Lines source6 = {"abc def"};
    auto r7 = oracle_->simulate(source6, 0, 1, "dw");
    cerr << "  'abc def' dw at [0,1] ('b'): '" << r7.lines.flatten() << "' cursor=[" << r7.row << "," << r7.col << "]" << endl;

    auto r8 = oracle_->simulate(source6, 0, 2, "dw");
    cerr << "  'abc def' dw at [0,2] ('c'): '" << r8.lines.flatten() << "' cursor=[" << r8.row << "," << r8.col << "]" << endl;
  }

  cerr << endl << "=== Sentence motion basics ===" << endl;
  {
    // Test what constitutes a sentence boundary
    Lines source = {"Hello. World", "Next line."};
    auto r1 = oracle_->simulate(source, 0, 7, "d(");  // cursor on 'W'
    cerr << "  'Hello. World' d( at col 7 ('W'): '" << r1.lines.flatten() << "'" << endl;

    auto r2 = oracle_->simulate(source, 0, 0, "d)");  // cursor on 'H'
    cerr << "  'Hello. World' d) at col 0 ('H'): '" << r2.lines.flatten() << "'" << endl;

    // No punctuation - is newline a sentence boundary?
    Lines source2 = {"abc", "def"};
    auto r3 = oracle_->simulate(source2, 1, 0, "d(");
    cerr << "  'abc'/'def' d( at [1,0]: '" << r3.lines.flatten() << "'" << endl;
  }
}

TEST_F(NeovimOracleDebug, DISABLED_InvestigateDAWEdgeCases) {
  // Investigate: when cursor is on a word with leading whitespace (but cursor NOT on whitespace),
  // does daw include leading whitespace from the previous line?

  cerr << "=== Case 1: Word at line start with leading space, cursor on word ===" << endl;
  // " eceba" - cursor on 'e' (col 1), 'c' (col 2), etc.
  {
    Lines source = {"abc", " def"};
    cerr << "Source: 'abc' / ' def'" << endl;

    // daw on 'd' (col 1) - word has leading space, no trailing space
    auto r1 = oracle_->simulate(source, 1, 1, "daw");
    cerr << "  daw at [1,1] ('d'): '" << r1.lines.flatten() << "'" << endl;

    // daw on 'e' (col 2)
    auto r2 = oracle_->simulate(source, 1, 2, "daw");
    cerr << "  daw at [1,2] ('e'): '" << r2.lines.flatten() << "'" << endl;
  }

  cerr << endl << "=== Case 2: Word at line start with NO leading space ===" << endl;
  {
    Lines source = {"abc", "def"};
    cerr << "Source: 'abc' / 'def'" << endl;

    auto r1 = oracle_->simulate(source, 1, 0, "daw");
    cerr << "  daw at [1,0] ('d'): '" << r1.lines.flatten() << "'" << endl;

    auto r2 = oracle_->simulate(source, 1, 1, "daw");
    cerr << "  daw at [1,1] ('e'): '" << r2.lines.flatten() << "'" << endl;
  }

  cerr << endl << "=== Case 3: Word with trailing space ===" << endl;
  {
    Lines source = {"abc", "def "};
    cerr << "Source: 'abc' / 'def '" << endl;

    auto r1 = oracle_->simulate(source, 1, 0, "daw");
    cerr << "  daw at [1,0] ('d'): '" << r1.lines.flatten() << "'" << endl;
  }

  cerr << endl << "=== Case 4: Word with leading AND trailing space ===" << endl;
  {
    Lines source = {"abc", " def "};
    cerr << "Source: 'abc' / ' def '" << endl;

    auto r1 = oracle_->simulate(source, 1, 1, "daw");
    cerr << "  daw at [1,1] ('d'): '" << r1.lines.flatten() << "'" << endl;
  }

  cerr << endl << "=== Case 5: Previous line has trailing space ===" << endl;
  {
    Lines source = {"abc ", "def"};
    cerr << "Source: 'abc ' / 'def'" << endl;

    auto r1 = oracle_->simulate(source, 1, 0, "daw");
    cerr << "  daw at [1,0] ('d'): '" << r1.lines.flatten() << "'" << endl;
  }

  cerr << endl << "=== Case 6: Previous line has trailing space, current has leading ===" << endl;
  {
    Lines source = {"abc ", " def"};
    cerr << "Source: 'abc ' / ' def'" << endl;

    auto r1 = oracle_->simulate(source, 1, 1, "daw");
    cerr << "  daw at [1,1] ('d'): '" << r1.lines.flatten() << "'" << endl;
  }

  cerr << endl << "=== Case 7: Single line tests ===" << endl;
  {
    // Compare single line behavior
    auto r1 = oracle_->simulate({" def"}, 0, 1, "daw");
    cerr << "  daw on ' def' at col 1: '" << r1.lines.flatten() << "'" << endl;

    auto r2 = oracle_->simulate({"def"}, 0, 0, "daw");
    cerr << "  daw on 'def' at col 0: '" << r2.lines.flatten() << "'" << endl;

    auto r3 = oracle_->simulate({"def "}, 0, 0, "daw");
    cerr << "  daw on 'def ' at col 0: '" << r3.lines.flatten() << "'" << endl;
  }

  cerr << endl << "=== Case 8: The failing test case ===" << endl;
  {
    Lines source = {" abe. ", " eceba"};
    cerr << "Source: ' abe. ' / ' eceba'" << endl;

    auto r1 = oracle_->simulate(source, 1, 2, "daw");
    cerr << "  daw at [1,2] ('c'): '" << r1.lines.flatten() << "'" << endl;

    // What about at col 1?
    auto r2 = oracle_->simulate(source, 1, 1, "daw");
    cerr << "  daw at [1,1] ('e'): '" << r2.lines.flatten() << "'" << endl;
  }

  cerr << endl << "=== Case 9: Verify single-line 'no trailing' behavior ===" << endl;
  {
    // Single line: leading space, no trailing
    auto r1 = oracle_->simulate({"  abc"}, 0, 2, "daw");
    cerr << "  daw on '  abc' at col 2 ('a'): '" << r1.lines.flatten() << "'" << endl;

    // Single line: no leading, no trailing
    auto r2 = oracle_->simulate({"abc"}, 0, 0, "daw");
    cerr << "  daw on 'abc' at col 0: '" << r2.lines.flatten() << "'" << endl;

    // What if there's a word AFTER? (trailing ws leads to next word)
    auto r3 = oracle_->simulate({"abc def"}, 0, 0, "daw");
    cerr << "  daw on 'abc def' at col 0 ('a'): '" << r3.lines.flatten() << "'" << endl;

    auto r4 = oracle_->simulate({"abc def"}, 0, 4, "daw");
    cerr << "  daw on 'abc def' at col 4 ('d'): '" << r4.lines.flatten() << "'" << endl;

    // Word in middle with spaces on both sides
    auto r5 = oracle_->simulate({" abc "}, 0, 1, "daw");
    cerr << "  daw on ' abc ' at col 1 ('a'): '" << r5.lines.flatten() << "'" << endl;
  }

  cerr << endl << "=== Case 10: End of line as 'trailing whitespace'? ===" << endl;
  {
    // Does newline count as trailing whitespace?
    Lines source = {"abc", "def"};
    auto r1 = oracle_->simulate(source, 0, 0, "daw");
    cerr << "  daw on 'abc'/'def' at [0,0]: '" << r1.lines.flatten() << "'" << endl;

    // Compare to having actual trailing space
    Lines source2 = {"abc ", "def"};
    auto r2 = oracle_->simulate(source2, 0, 0, "daw");
    cerr << "  daw on 'abc '/'def' at [0,0]: '" << r2.lines.flatten() << "'" << endl;
  }

  cerr << endl << "=== Case 11: Does having a word BEFORE matter? ===" << endl;
  {
    // Leading space with no word before
    auto r1 = oracle_->simulate({"  abc"}, 0, 2, "daw");
    cerr << "  '  abc' at col 2: '" << r1.lines.flatten() << "'" << endl;

    // Leading space with word before (on same line)
    auto r2 = oracle_->simulate({"x  abc"}, 0, 3, "daw");
    cerr << "  'x  abc' at col 3 ('a'): '" << r2.lines.flatten() << "'" << endl;

    // Leading space with word before (on previous line)
    auto r3 = oracle_->simulate({"xyz", "  abc"}, 1, 2, "daw");
    cerr << "  'xyz'/'  abc' at [1,2]: '" << r3.lines.flatten() << "'" << endl;

    // No leading space, no trailing space, word before on same line
    auto r4 = oracle_->simulate({"x abc"}, 0, 2, "daw");
    cerr << "  'x abc' at col 2 ('a'): '" << r4.lines.flatten() << "'" << endl;

    // Word at very start of line (col 0), previous line exists
    auto r5 = oracle_->simulate({"xyz", "abc"}, 1, 0, "daw");
    cerr << "  'xyz'/'abc' at [1,0]: '" << r5.lines.flatten() << "'" << endl;
  }

  cerr << endl << "=== Case 12: Trailing space detection ===" << endl;
  {
    // After word: space then another word
    auto r1 = oracle_->simulate({"abc def ghi"}, 0, 4, "daw");
    cerr << "  'abc def ghi' at col 4 ('d'): '" << r1.lines.flatten() << "'" << endl;

    // After word: just space (EOL)
    auto r2 = oracle_->simulate({"abc def "}, 0, 4, "daw");
    cerr << "  'abc def ' at col 4 ('d'): '" << r2.lines.flatten() << "'" << endl;

    // After word: newline (next line exists)
    auto r3 = oracle_->simulate({"abc def", "ghi"}, 0, 4, "daw");
    cerr << "  'abc def'/'ghi' at [0,4] ('d'): '" << r3.lines.flatten() << "'" << endl;
  }
}

TEST_F(NeovimOracleDebug, DISABLED_InvestigateParagraph) {
  // Known issue: d{ is LINEWISE in Neovim but our optimizer treats it as characterwise
  // This causes the optimizer to produce incorrect deletion sequences
  // See: https://github.com/... (future issue tracker link)

  Lines source = {"d..,b,b", "eafecf  ", ".f ,b,f"};
  cerr << "Source lines:" << endl;
  for (size_t i = 0; i < source.size(); i++) {
    cerr << "  [" << i << "]: '" << source[i] << "' (len=" << source[i].size() << ")" << endl;
  }

  auto tracer = makeTracer(source, 1, 7);  // pos [1,7]

  // Trace step by step
  tracer.trace("d{");
  tracer.trace("dE");
  tracer.trace("D");

  tracer.printSummary();

  // Also test full sequence
  cerr << endl;
  auto tracer2 = makeTracer(source, 1, 7);
  tracer2.traceFullSequence("d{dED");
}

TEST_F(NeovimOracleDebug, DISABLED_InvestigateDAW) {
  // FAIL iter=0 pos=[1,0] seq='dawdd'
  // Source:  abe.
  //  eceba
  // Result:  abe.

  // First, understand the source buffer
  Lines source = {" abe. ", " eceba"};
  cerr << "Source lines:" << endl;
  for (size_t i = 0; i < source.size(); i++) {
    cerr << "  [" << i << "]: '" << source[i] << "' (len=" << source[i].size() << ")" << endl;
  }

  auto tracer = makeTracer(source, 1, 0);  // pos [1,0]

  // Trace step by step
  tracer.trace("daw");
  tracer.trace("dd");

  tracer.printSummary();

  // Also test full sequence
  cerr << endl;
  auto tracer2 = makeTracer(source, 1, 0);
  tracer2.traceFullSequence("dawdd");

  // The optimizer expected this to delete everything
  // Let's see what our VimCore simulation says
  cerr << endl << "=== Our VimCore simulation ===" << endl;
  Lines testLines = source;
  Position pos(1, 0);

  // Simulate daw
  cerr << "Before daw: " << testLines << " pos=(" << pos.line << "," << pos.col << ")" << endl;
  auto range = VimCore::textObject(pos, testLines, false, false);  // daw
  cerr << "daw range: [" << range.first.line << "," << range.first.col << "] to ["
       << range.last.line << "," << range.last.col << "]" << endl;

  // Test various daw scenarios to understand Neovim's behavior
  cerr << endl << "=== Additional daw tests ===" << endl;

  // Case 1: daw on space at start of line
  auto r1 = oracle_->simulate({" hello"}, 0, 0, "daw");
  cerr << "daw on ' hello' at col 0: '" << r1.lines.flatten() << "'" << endl;

  // Case 2: daw on word with leading space (same line)
  auto r2 = oracle_->simulate({"abc def"}, 0, 4, "daw");
  cerr << "daw on 'abc def' at col 4 (d): '" << r2.lines.flatten() << "'" << endl;

  // Case 3: daw on space between lines
  auto r3 = oracle_->simulate({"abc ", " def"}, 1, 0, "daw");
  cerr << "daw on 'abc ','  def' at [1,0]: '" << r3.lines.flatten() << "'" << endl;

  // Case 4: daw on only whitespace at start of line
  auto r4 = oracle_->simulate({"abc", "  def"}, 1, 0, "daw");
  cerr << "daw on 'abc','  def' at [1,0]: '" << r4.lines.flatten() << "'" << endl;

  // Case 5: daw on only whitespace at start of line (1 space)
  auto r5 = oracle_->simulate({"abc", " def"}, 1, 0, "daw");
  cerr << "daw on 'abc',' def' at [1,0]: '" << r5.lines.flatten() << "'" << endl;
}

TEST_F(NeovimOracleDebug, DISABLED_InvestigateJCursor) {
  cerr << "=== J cursor placement investigation ===" << endl;

  // Test case from the failing scenario
  Lines lines = {"ccbbfd ", " c"};
  cerr << "Before J: '" << lines[0] << "' / '" << lines[1] << "'" << endl;
  cerr << "Cursor at [0,6]" << endl;

  auto r = oracle_->simulate(lines, 0, 6, "J");
  cerr << endl << "Neovim after J:" << endl;
  cerr << "  Buffer: '" << r.lines.flatten() << "'" << endl;
  cerr << "  Cursor: [" << r.row << "," << r.col << "]" << endl;

  Lines ourLines = lines;
  Position pos(0, 6);
  VimCore::joinLines(ourLines, pos, true);
  cerr << endl << "Our VimCore after J:" << endl;
  cerr << "  Buffer: '" << ourLines.flatten() << "'" << endl;
  cerr << "  Cursor: [" << pos.line << "," << pos.col << "]" << endl;

  if (pos.col != r.col) {
    cerr << endl << "*** CURSOR MISMATCH! Neovim=" << r.col << " Ours=" << pos.col << " ***" << endl;
  }

  cerr << endl << "=== More J tests ===" << endl;

  auto testJ = [&](Lines l, int startCol, const string& label) {
    auto nvim = oracle_->simulate(l, 0, startCol, "J");
    Lines our = l;
    Position p(0, startCol);
    VimCore::joinLines(our, p, true);
    cerr << label << " J at [0," << startCol << "]:" << endl;
    cerr << "  Neovim: '" << nvim.lines.flatten() << "' cursor=[" << nvim.row << "," << nvim.col << "]" << endl;
    cerr << "  Ours:   '" << our.flatten() << "' cursor=[" << p.line << "," << p.col << "]" << endl;
    if (p.col != nvim.col) {
      cerr << "  *** MISMATCH ***" << endl;
    }
  };

  testJ({"abc", "def"}, 2, "'abc'/'def'");
  testJ({"abc ", "def"}, 2, "'abc '/'def'");
  testJ({"abc", " def"}, 2, "'abc'/' def'");
  testJ({"abc ", " def"}, 2, "'abc '/' def'");
  testJ({"abc  ", "def"}, 2, "'abc  '/'def' (2 trailing spaces)");
}
