// OldDebugTest.cpp - Previously used debug tests that were important for finding bugs

#include <gtest/gtest.h>

// #include "Optimizer/Config.h"
// #include "Optimizer/EditOptimizer.h"
// #include "Optimizer/BufferIndex.h"
// #include "Boundary/EditBoundary.h"
// #include "Utils/NeovimOracle.h"
// #include "VimCore/VimCore.h"
// #include "VimCore/VimEndpointUtils.h"
// #include "VimCore/VimMotionUtils.h"
// #include "VimCore/SentenceEdgeType.h"
// #include "Editor/Motion.h"
// #include "Editor/Edit.h"

using namespace std;

/*
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

TEST_F(NeovimOracleDebug, DISABLED_DebugDeOnSingleCharLine) {
  // What does de do on a single-char line followed by other lines?
  Lines buffer = {"bcbfbb", "a", "bcced", "fabdfbe"};

  cerr << "Initial: " << buffer << endl;

  auto r = oracle_->simulate(buffer, 1, 0, "de");
  cerr << "After 'de' at (1,0):" << endl;
  cerr << "  Lines: " << r.lines << endl;
  cerr << "  Cursor: (" << r.row << ", " << r.col << ")" << endl;
}

TEST_F(NeovimOracleDebug, DISABLED_DebugDAtColZero) {
  cerr << "\n--- D from col 0 ---" << endl;

  auto r1 = oracle_->simulate({"abc", "def"}, 1, 0, "D");
  cerr << "D from (1, 0) on 'd' in ['abc', 'def']:" << endl;
  cerr << "  Lines: " << r1.lines << endl;
  cerr << "  Cursor: (" << r1.row << ", " << r1.col << ")" << endl;
}

TEST_F(NeovimOracleDebug, DISABLED_DebugDbAtColZero) {
  cerr << "\n--- db from col 0 ---" << endl;

  auto r1 = oracle_->simulate({"abc", "def", "ghi"}, 2, 0, "db");
  cerr << "db from (2, 0) on 'g' in ['abc', 'def', 'ghi']:" << endl;
  cerr << "  Lines: " << r1.lines << endl;
  cerr << "  Cursor: (" << r1.row << ", " << r1.col << ")" << endl;
}

TEST_F(NeovimOracleDebug, DISABLED_DebugTargetColWithKAndDd) {
  // Test to verify targetCol behavior with k followed by dd
  // This was a bug where dd was resetting targetCol incorrectly

  auto tracer = makeTracer({"fcef", "cdd", "bbfbfb"}, 2, 4);

  tracer.trace("x");   // Delete char - stays at col 4
  tracer.trace("k");   // Move up - col clamps to 2, targetCol should stay 4
  tracer.trace("dd");  // Delete line - in vim, dd resets targetCol

  tracer.printSummary();

  // Also test full sequence
  tracer.traceFullSequence("xkdd");
}

TEST_F(NeovimOracleDebug, DISABLED_DebugMultipleDd) {
  // Test dd behavior without motions in between
  // dd resets targetCol to the clamped column

  auto tracer = makeTracer({"acadcfac", "dccc", "abf", "fbcbacaf"}, 1, 3);

  tracer.trace("dd");  // First dd
  tracer.trace("dd");  // Second dd - should NOT use original targetCol=3

  tracer.printSummary();

  // Compare with full sequence
  tracer.traceFullSequence("dddd");
}

// ============================================================================
// Sub-buffer boundary failure investigation
// ============================================================================

TEST_F(NeovimOracleDebug, DISABLED_InvestigateSubBufferFailure_Sentence) {
  // Failure #2: ")F " escapes sub-buffer
  // Sub-buffer is lines 1-4 of full buffer
  // Start: sub(2,10) = full(3,10)

  Lines fullBuffer = {
    " ca . dc.b ,dda ba",
    "dba,dd,acaaba,ccbcac.bddca,.ac",
    "ccd,d ,.d,ac ,bcac,c.d,bddddb.",
    "..dcc.b bbadc b",
    ",.ddcb  a,db .",
    "bc.,.,c cd. db.cba.d",
    "d,ba, , .cdd,aa..daa. bd",
    "a,,b,dd,ad.,d ccdcdbd.dc d,d"
  };

  Lines subBuffer = {
    "dba,dd,acaaba,ccbcac.bddca,.ac",
    "ccd,d ,.d,ac ,bcac,c.d,bddddb.",
    "..dcc.b bbadc b",
    ",.ddcb  a,db ."
  };

  cerr << "\n=== Full Buffer Trace (start at line 3, col 10) ===" << endl;
  auto fullTracer = makeTracer(fullBuffer, 3, 10);
  fullTracer.trace(")");
  fullTracer.printSummary();

  cerr << "\n=== Sub-Buffer Trace (start at line 2, col 10) ===" << endl;
  auto subTracer = makeTracer(subBuffer, 2, 10);
  subTracer.trace(")");
  subTracer.printSummary();

  cerr << "\n=== Analysis ===" << endl;
  cerr << "The ) motion finds different sentence boundaries because:" << endl;
  cerr << "- Full buffer has more content below to search" << endl;
  cerr << "- Sub-buffer clamps at its last line" << endl;
}

TEST_F(NeovimOracleDebug, DISABLED_TraceSentence2ParenFailure) {
  // From stress test failure:
  // Sequence: "2(l"
  // Start: sub(3,21) = full(7,21)
  // Our prediction: (3, 15)
  // Neovim in sub-buffer coords: (2, 7)

  Lines fullBuffer = {
    "cbd..,cdbb a.aa.",
    "ca,c.db.  d  c.",
    ".d.,b aa. acc aa bb. b,.aba",
    "c ,dbccbaa.cc .,,,b...a",
    ",b ,, adbacdcdb,cdacc,.",
    "dcaac.a,,ba,",
    "c,,.. ..a,, .dc .db ,dab",
    "adaabbaaddbc. acbc.. aa,"
  };

  Lines subBuffer = {
    ",b ,, adbacdcdb,cdacc,.",
    "dcaac.a,,ba,",
    "c,,.. ..a,, .dc .db ,dab",
    "adaabbaaddbc. acbc.. aa,"
  };

  cerr << "\n=== Line content analysis ===" << endl;
  cerr << "Sub line 3: \"" << subBuffer[3] << "\"" << endl;
  for (int i = 0; i < (int)subBuffer[3].size(); i++) {
    cerr << i << "=" << subBuffer[3][i] << " ";
  }
  cerr << endl;

  cerr << "\n=== Neovim sentence motion comparison ===" << endl;

  // Single ( from start position
  auto result1 = oracle_->simulate(fullBuffer, 7, 21, "(");
  cerr << "Full buffer: ( from (7,21) -> (" << result1.row << ", " << result1.col << ")" << endl;

  auto subResult1 = oracle_->simulate(subBuffer, 3, 21, "(");
  cerr << "Sub buffer:  ( from (3,21) -> (" << subResult1.row << ", " << subResult1.col << ")" << endl;

  // Second ( from first result
  auto result2 = oracle_->simulate(fullBuffer, result1.row, result1.col, "(");
  cerr << "Full buffer: ( from (" << result1.row << "," << result1.col << ") -> (" << result2.row << ", " << result2.col << ")" << endl;

  auto subResult2 = oracle_->simulate(subBuffer, subResult1.row, subResult1.col, "(");
  cerr << "Sub buffer:  ( from (" << subResult1.row << "," << subResult1.col << ") -> (" << subResult2.row << ", " << subResult2.col << ")" << endl;

  // Full 2( sequence
  auto result2paren = oracle_->simulate(fullBuffer, 7, 21, "2(");
  cerr << "Full buffer: 2( from (7,21) -> (" << result2paren.row << ", " << result2paren.col << ")" << endl;

  auto subResult2paren = oracle_->simulate(subBuffer, 3, 21, "2(");
  cerr << "Sub buffer:  2( from (3,21) -> (" << subResult2paren.row << ", " << subResult2paren.col << ")" << endl;

  // Our simulateMotions
  cerr << "\n=== Our simulateMotions ===" << endl;
  Position sim1 = simulateMotions(Position(3, 21), "(", subBuffer);
  cerr << "simulateMotions((3,21), \"(\") = (" << sim1.line << ", " << sim1.col << ")" << endl;

  Position sim2 = simulateMotions(sim1, "(", subBuffer);
  cerr << "simulateMotions((" << sim1.line << "," << sim1.col << "), \"(\") = (" << sim2.line << ", " << sim2.col << ")" << endl;

  Position sim2paren = simulateMotions(Position(3, 21), "2(", subBuffer);
  cerr << "simulateMotions((3,21), \"2(\") = (" << sim2paren.line << ", " << sim2paren.col << ")" << endl;

  // BufferIndex sentence positions
  cerr << "\n=== BufferIndex sentence positions ===" << endl;
  BufferIndex subIdx(subBuffer);
  cerr << "Sub buffer sentence starts:" << endl;
  for (const auto& pos : subIdx.getPositions(LandingType::Sentence)) {
    char c = pos.col < (int)subBuffer[pos.line].size() ? subBuffer[pos.line][pos.col] : '?';
    cerr << "  (" << pos.line << ", " << pos.col << ") = '" << c << "'" << endl;
  }

  cerr << "\n=== Analysis ===" << endl;
  bool match = (sim2paren.line == subResult2paren.row && sim2paren.col == subResult2paren.col);
  cerr << "2( matches Neovim sub-buffer: " << (match ? "YES" : "NO") << endl;
}

TEST_F(NeovimOracleDebug, DISABLED_TraceSentenceMixedContentFailure) {
  // First, let's understand what Neovim thinks about ". . c" patterns
  cerr << "\n=== Testing '. . c' pattern ===" << endl;
  Lines testPattern = {". . c"};
  cerr << "Buffer: \"" << testPattern[0] << "\"" << endl;
  cerr << "Positions: 0=. 1=space 2=. 3=space 4=c" << endl;

  // What does ( do from position 4 (c)?
  auto tp0 = oracle_->simulate(testPattern, 0, 4, "(");
  cerr << "( from (0,4) 'c' -> (" << tp0.row << ", " << tp0.col << ") = '" << testPattern[0][tp0.col] << "'" << endl;

  // What does ( do from position 2 (second .)?
  auto tp1 = oracle_->simulate(testPattern, 0, 2, "(");
  cerr << "( from (0,2) '.' -> (" << tp1.row << ", " << tp1.col << ") = '" << testPattern[0][tp1.col] << "'" << endl;

  // What does ( do from position 0 (first .)?
  auto tp2 = oracle_->simulate(testPattern, 0, 0, "(");
  cerr << "( from (0,0) '.' -> (" << tp2.row << ", " << tp2.col << ") = '" << testPattern[0][tp2.col] << "'" << endl;

  // Test "d . c" pattern - '.' after letter should NOT be a sentence start
  cerr << "\n=== Testing 'd . c' pattern ===" << endl;
  Lines testPattern2 = {"d . c"};
  cerr << "Buffer: \"" << testPattern2[0] << "\"" << endl;
  cerr << "Positions: 0=d 1=space 2=. 3=space 4=c" << endl;

  auto tp3 = oracle_->simulate(testPattern2, 0, 4, "(");
  cerr << "( from (0,4) 'c' -> (" << tp3.row << ", " << tp3.col << ") = '" << testPattern2[0][tp3.col] << "'" << endl;

  auto tp4 = oracle_->simulate(testPattern2, 0, 2, "(");
  cerr << "( from (0,2) '.' -> (" << tp4.row << ", " << tp4.col << ") = '" << testPattern2[0][tp4.col] << "'" << endl;

  // Now test the original failing case
  cerr << "\n=== Original Failing Case ===" << endl;

  Lines subBuffer = {
    "   .d e.,.b,,",
    "e ,, .,.a .f.a  cf .d,,e f, b ",
    "c d ,. ,,. e . , ac.e  c  a ",
    "  .  d .  d , ,be. .c,d  dfb"
  };

  cerr << "\n=== Buffer Content ===" << endl;
  for (size_t i = 0; i < subBuffer.size(); i++) {
    cerr << "[" << i << "]: \"" << subBuffer[i] << "\"" << endl;
    cerr << "     ";
    for (size_t j = 0; j < subBuffer[i].size(); j++) {
      cerr << j % 10;
    }
    cerr << endl;
  }

  cerr << "\n=== Neovim sentence motions ===" << endl;

  // Test single ( from (3, 20)
  auto r1 = oracle_->simulate(subBuffer, 3, 20, "(");
  cerr << "Neovim: ( from (3,20) -> (" << r1.row << ", " << r1.col << ")" << endl;

  Position sim1 = simulateMotions(Position(3, 20), "(", subBuffer);
  cerr << "Ours:   ( from (3,20) -> (" << sim1.line << ", " << sim1.col << ")" << endl;

  // Step through multiple ( motions
  cerr << "\n=== Multiple ( motions from (3,20) ===" << endl;
  Position pos(3, 20);
  int row = 3, col = 20;
  for (int i = 1; i <= 6; i++) {
    auto nvim = oracle_->simulate(subBuffer, row, col, "(");
    Position ours = simulateMotions(pos, "(", subBuffer);

    bool match = (nvim.row == ours.line && nvim.col == ours.col);
    cerr << i << "(: neovim=(" << nvim.row << "," << nvim.col << ") "
         << "ours=(" << ours.line << "," << ours.col << ") "
         << (match ? "MATCH" : "MISMATCH") << endl;

    row = nvim.row;
    col = nvim.col;
    pos = ours;
  }

  // Test VimCore::motionSentenceEndpoint directly
  cerr << "\n=== VimCore::motionSentenceEndpoint trace ===" << endl;
  using namespace VimCore;

  Position ep = motionSentenceEndpoint(Position(3, 20), subBuffer, false, SentenceEdgeType::NextEdge);
  cerr << "motionSentenceEndpoint((3,20), backward, NextEdge) = (" << ep.line << ", " << ep.col << ")" << endl;

  // Test what findCurrentSentenceStart returns
  cerr << "\n=== findCurrentSentenceStart trace ===" << endl;
  auto [sl, sc] = VimCore::findCurrentSentenceStart(subBuffer, 3, 20);
  cerr << "findCurrentSentenceStart((3,20)) = (" << sl << ", " << sc << ")" << endl;

  // What char is at position (3, 20)?
  cerr << "\nChar at (3,20): '" << subBuffer[3][20] << "'" << endl;
  cerr << "Line 3: \"" << subBuffer[3] << "\"" << endl;

  // Analyze sentence boundaries on line 3
  cerr << "\n=== Sentence ends on line 3 ===" << endl;
  for (int c = 0; c < (int)subBuffer[3].size(); c++) {
    if (VimCore::isSentenceEndAt(subBuffer, 3, c)) {
      cerr << "Sentence end at (3, " << c << ") = '" << subBuffer[3][c] << "'" << endl;
    }
  }

  EXPECT_TRUE(false) << "Debug test - check output above";
}

TEST_F(DebugTest, DISABLED_InvestigateSingleLineSurrounded) {
  // From Boundary_SingleLineSurrounded failure
  Lines fullBuffer = {"xx", "hello", "xx"};
  Position startPos(1, 0), endPos(1, 4);
  Lines editRegion = fullBuffer.getSpan(startPos, endPos);
  EditBoundary boundary(fullBuffer, startPos, endPos);

  cerr << "\n=== SingleLineSurrounded Investigation ===" << endl;
  cerr << "fullBuffer: " << fullBuffer << endl;
  cerr << "editRegion: " << editRegion << endl;
  cerr << "hasLinesAbove: " << boundary.hasLinesAbove() << endl;
  cerr << "hasLinesBelow: " << boundary.hasLinesBelow() << endl;
  cerr << "prefix: '" << boundary.prefix() << "'" << endl;
  cerr << "suffix: '" << boundary.suffix() << "'" << endl;

  EditOptimizer opt(config, OptimizerParams(30, 1e5, 1.0, 2.0));
  EditResult res = opt.optimizeEdit(editRegion, {""}, boundary);

  cerr << "\nResults (typeAllResults):" << endl;
  for (int i = 0; i < static_cast<int>(res.typeAllResults.size()); i++) {
    const auto& r = res.typeAllResults[i];
    if (r.isValid()) {
      cerr << "  " << i << ": " << r.getSequenceString() << " (cost=" << r.keyCost << ")" << endl;
    } else {
      cerr << "  " << i << ": INVALID" << endl;
    }
  }
}

TEST_F(DebugTest, DISABLED_InvestigatePosition11) {
  // Position 11 finds d{de (cost 5) instead of dddd (cost 4) with 10K iterations
  // With 1M iterations, finds d{D (cost 3) - actually better!
  // Conclusion: The fix works, but search budget affects results
  Lines initialLines = {"aa bb", "arst neio"};
  EditBoundary boundary(initialLines, {0, 0}, initialLines.lastPos());

  // Position 11 is line 1, col 6 ('e' in "arst neio")
  cerr << "\n=== Position 11 Investigation ===" << endl;
  cerr << "Buffer: " << initialLines << endl;
  cerr << "Position 11 is (1, 6) = '" << initialLines[1][6] << "'" << endl;

  // Trace the optimal d{D sequence
  Lines buf = initialLines;
  Position pos(1, 6);
  Mode mode = Mode::Normal;

  cerr << "\nTracing d{D from (1,6):" << endl;
  for (const auto& op : Edit::parseEdits("d{D")) {
    cerr << "  Before: " << buf << " pos=" << pos << endl;
    Edit::applyEdit(buf, pos, mode, op);
    cerr << "  After '" << op.edit << "': " << buf << " pos=" << pos << endl;
  }
  cerr << "Final buffer empty? " << buf.isEmpty() << endl;

  // Compare 10K vs 1M iterations
  cerr << "\n=== Comparing iteration limits ===" << endl;

  EditOptimizer opt10k(config, OptimizerParams(30, 1e4, 1.0, 2.0));
  vector<Result> res10k = opt10k.optimizePureDeletion(initialLines, boundary);
  cerr << "10K iterations: " << res10k[11].getSequenceString()
       << " (cost=" << res10k[11].keyCost << ")" << endl;

  EditOptimizer opt1m(config, OptimizerParams(30, 1e6, 1.0, 2.0));
  vector<Result> res1m = opt1m.optimizePureDeletion(initialLines, boundary);
  cerr << "1M iterations:  " << res1m[11].getSequenceString()
       << " (cost=" << res1m[11].keyCost << ")" << endl;
}

TEST_F(NeovimOracleDebug, DISABLED_InvestigateJWithSuffix) {
  // From OutputCorrectnessTest.MultiLineEmbedded failure:
  // FAIL iter=7 editPos=[0,2] bufferPos=[0,4] seq='XXJgJXXxxxxxdge'
  //   FullBuffer: ffade\nfbdc\ndaeaad
  //   EditRegion: ade\nfbdc\ndaeaa
  //   Expected: 'ffd'
  //   Got: 'ff'
  //
  // prefix = "ff", suffix = "d"
  // The suffix 'd' is being deleted when it shouldn't be

  Lines fullBuffer = {"ffade", "fbdc", "daeaad"};

  cerr << "\n=== Comparing Neovim vs our applyEdit step-by-step ===" << endl;
  cerr << "FullBuffer: " << fullBuffer << endl;
  cerr << "Start: (0, 4)" << endl;

  // Test individual commands
  vector<string> cmds = {"X", "X", "J", "gJ", "X", "X", "x", "x", "x", "x", "x", "dge"};

  // Track Neovim state
  Lines nvimBuf = fullBuffer;
  int nvimRow = 0, nvimCol = 4;

  // Track our state
  Lines ourBuf = fullBuffer;
  Position ourPos(0, 4);
  Mode ourMode = Mode::Normal;

  for (const auto& cmd : cmds) {
    // Neovim
    auto nvim = oracle_->simulate(nvimBuf, nvimRow, nvimCol, cmd);

    // Ours
    for (const auto& op : Edit::parseEdits(cmd)) {
      Edit::applyEdit(ourBuf, ourPos, ourMode, op);
    }

    bool linesMatch = (ourBuf == nvim.lines);
    bool cursorMatch = (ourPos.line == nvim.row && ourPos.col == nvim.col);

    cerr << "'" << cmd << "': ";
    if (!linesMatch || !cursorMatch) {
      cerr << "MISMATCH!" << endl;
      cerr << "  Neovim: buf='" << nvim.lines.flatten() << "' cursor=(" << nvim.row << "," << nvim.col << ")" << endl;
      cerr << "  Ours:   buf='" << ourBuf.flatten() << "' cursor=(" << ourPos.line << "," << ourPos.col << ")" << endl;
      cerr << "  targetCol=" << ourPos.targetCol << endl;
    } else {
      cerr << "OK buf='" << nvim.lines.flatten() << "' cursor=(" << nvim.row << "," << nvim.col << ")" << endl;
    }

    nvimBuf = nvim.lines;
    nvimRow = nvim.row;
    nvimCol = nvim.col;
  }
}

TEST_F(NeovimOracleDebug, DISABLED_TraceSentenceIndexFailure) {
  // From MotionOptimizerBoundaryStress failure:
  // Sequence: "2(b"
  // Start: sub(3,6) = full(7,6)
  // Our prediction (sub-buffer): (1, 22)
  // Neovim in sub-buffer coords: (2, 14)

  Lines fullBuffer = {
    "c,,b.d a, ",
    "cd,b,dc.,d, .a..b.  a,a.,,",
    ",... ,a,dbdaccca",
    "a,c..cdaca cbaaa.,  .caa.d ad,",
    "c,c c a, ..d  ac bd ,,c",
    "cc d bd,d,d ,.d., da,d. c ,ca",
    ",b da   , abdb. . cbb ad  d.,",
    "aa .a b d.d. "
  };

  Lines subBuffer = {
    "c,c c a, ..d  ac bd ,,c",
    "cc d bd,d,d ,.d., da,d. c ,ca",
    ",b da   , abdb. . cbb ad  d.,",
    "aa .a b d.d. "
  };

  // Test simulateMotions directly
  cerr << "\n=== Direct simulateMotions Test ===" << endl;
  Position simResult = simulateMotions(Position(3, 6), "2(b", subBuffer);
  cerr << "simulateMotions((3,6), \"2(b\", subBuffer) = (" << simResult.line << ", " << simResult.col << ")" << endl;

  // Step by step with simulateMotions
  Position step1 = simulateMotions(Position(3, 6), "(", subBuffer);
  cerr << "simulateMotions((3,6), \"(\", subBuffer) = (" << step1.line << ", " << step1.col << ")" << endl;

  Position step2 = simulateMotions(step1, "(", subBuffer);
  cerr << "simulateMotions((" << step1.line << "," << step1.col << "), \"(\", subBuffer) = (" << step2.line << ", " << step2.col << ")" << endl;

  Position step3 = simulateMotions(step2, "b", subBuffer);
  cerr << "simulateMotions((" << step2.line << "," << step2.col << "), \"b\", subBuffer) = (" << step3.line << ", " << step3.col << ")" << endl;

  cerr << "\n=== BufferIndex Sentence Positions ===" << endl;

  cerr << "\n--- Full Buffer ---" << endl;
  BufferIndex fullIdx(fullBuffer);
  cerr << "Sentence starts in full buffer:" << endl;
  for (const auto& pos : fullIdx.getPositions(LandingType::Sentence)) {
    cerr << "  (" << pos.line << ", " << pos.col << ") = '"
         << (pos.col < (int)fullBuffer[pos.line].size() ? fullBuffer[pos.line][pos.col] : '?')
         << "'" << endl;
  }

  cerr << "\n--- Sub Buffer ---" << endl;
  BufferIndex subIdx(subBuffer);
  cerr << "Sentence starts in sub buffer:" << endl;
  for (const auto& pos : subIdx.getPositions(LandingType::Sentence)) {
    cerr << "  (" << pos.line << ", " << pos.col << ") = '"
         << (pos.col < (int)subBuffer[pos.line].size() ? subBuffer[pos.line][pos.col] : '?')
         << "'" << endl;
  }

  cerr << "\n=== Neovim Sentence Motion Comparison ===" << endl;

  // Test ( from start position in both buffers
  cerr << "\n--- Single ( motion ---" << endl;
  auto fullResult1 = oracle_->simulate(fullBuffer, 7, 6, "(");
  cerr << "Full buffer: ( from (7,6) -> (" << fullResult1.row << ", " << fullResult1.col << ")" << endl;

  auto subResult1 = oracle_->simulate(subBuffer, 3, 6, "(");
  cerr << "Sub buffer:  ( from (3,6) -> (" << subResult1.row << ", " << subResult1.col << ")" << endl;

  cerr << "\n--- Double 2( motion ---" << endl;
  auto fullResult2 = oracle_->simulate(fullBuffer, 7, 6, "2(");
  cerr << "Full buffer: 2( from (7,6) -> (" << fullResult2.row << ", " << fullResult2.col << ")" << endl;

  auto subResult2 = oracle_->simulate(subBuffer, 3, 6, "2(");
  cerr << "Sub buffer:  2( from (3,6) -> (" << subResult2.row << ", " << subResult2.col << ")" << endl;

  cerr << "\n--- Full sequence 2(b ---" << endl;
  auto fullResult3 = oracle_->simulate(fullBuffer, 7, 6, "2(b");
  cerr << "Full buffer: 2(b from (7,6) -> (" << fullResult3.row << ", " << fullResult3.col << ")" << endl;

  auto subResult3 = oracle_->simulate(subBuffer, 3, 6, "2(b");
  cerr << "Sub buffer:  2(b from (3,6) -> (" << subResult3.row << ", " << subResult3.col << ")" << endl;

  // What our optimizer would predict
  cerr << "\n--- getTwoClosest results for sentence ---" << endl;
  Position goal(0, 0);  // backwards search
  auto [under, over] = subIdx.getTwoClosest(LandingType::Sentence, Position(3, 6), goal);
  if (under.valid()) {
    cerr << "Undershoot: count=" << under.count << " pos=(" << under.pos.line << ", " << under.pos.col << ")" << endl;
  }
  if (over.valid()) {
    cerr << "Overshoot: count=" << over.count << " pos=(" << over.pos.line << ", " << over.pos.col << ")" << endl;
  }

  // Trace VimCore::motionSentenceEndpoint to compare with Neovim
  cerr << "\n=== VimCore::motionSentenceEndpoint trace ===" << endl;

  using namespace VimCore;

  // Single ( from (3, 6)
  Position ep1 = motionSentenceEndpoint(Position(3, 6), subBuffer, false, SentenceEdgeType::NextEdge);
  cerr << "motionSentenceEndpoint((3,6), backward) = (" << ep1.line << ", " << ep1.col << ")" << endl;
  cerr << "Neovim ( from (3,6) = (" << subResult1.row << ", " << subResult1.col << ")" << endl;

  // Single ( from ep1
  Position ep2 = motionSentenceEndpoint(ep1, subBuffer, false, SentenceEdgeType::NextEdge);
  cerr << "motionSentenceEndpoint((" << ep1.line << "," << ep1.col << "), backward) = (" << ep2.line << ", " << ep2.col << ")" << endl;

  // b from ep2
  Position pos_after_b = ep2;
  motionB(pos_after_b, subBuffer, false);  // Use motionB which sets skipCurrent=true
  cerr << "After b from (" << ep2.line << "," << ep2.col << ") = (" << pos_after_b.line << ", " << pos_after_b.col << ")" << endl;
  cerr << "Neovim 2(b from (3,6) = (" << subResult3.row << ", " << subResult3.col << ")" << endl;

  cerr << "\n=== Analysis ===" << endl;
  if (ep1.line == subResult1.row && ep1.col == subResult1.col) {
    cerr << "Single ( matches Neovim ✓" << endl;
  } else {
    cerr << "Single ( MISMATCH with Neovim ✗" << endl;
  }
  if (pos_after_b.line == subResult3.row && pos_after_b.col == subResult3.col) {
    cerr << "2(b matches Neovim ✓" << endl;
  } else {
    cerr << "2(b MISMATCH: ours=(" << pos_after_b.line << "," << pos_after_b.col
         << ") vs neovim=(" << subResult3.row << "," << subResult3.col << ") ✗" << endl;
  }
}
*/
