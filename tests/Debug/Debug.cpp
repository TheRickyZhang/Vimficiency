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
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Optimizer/CompositionOptimizer/CompositionSearchContext.h"
#include "Optimizer/CompositionOptimizer/DiffState.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"
#include "Boundary/EditBoundary.h"
#include "Boundary/MotionBoundary.h"
#include "Keyboard/MotionToKeys.h"
#include "Utils/NeovimOracle.h"
#include "VimCore/VimEditUtils.h"
#include "VimCore/VimEndpointUtils.h"

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
    return EditBoundary(source, Position(0, 0), source.endPos());
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


// Test that CompositionSearchContext merges adjacent pure insertions

TEST_F(NeovimOracleDebug, InvestigateTextObjectShortcuts) {
  cerr << "=== Text Object Shortcuts Investigation ===" << endl;

  // Test 1: ci( behavior from different positions
  cerr << endl << "=== ci( from different positions ===" << endl;
  {
    Lines source = {"foo ((hello)) bar"};
    cerr << "Source: '" << source[0] << "'" << endl;
    cerr << "Inner parens at 5,11; outer at 4,12" << endl;
    cerr << "Edit region for 'hello'->'goodbye' would be [6,11)" << endl;

    for (int col = 0; col <= 5; col++) {
      auto r = oracle_->simulate(source, 0, col, "ci(goodbye<Esc>");
      cerr << "  ci( at col " << col << ": '" << r.lines[0] << "'" << endl;
    }
  }

  // Test 2: ci" from different positions
  cerr << endl << "=== ci\" from different positions ===" << endl;
  {
    Lines source = {"foo \"hello\" bar"};
    cerr << "Source: '" << source[0] << "'" << endl;
    cerr << "Quotes at cols 4 and 10" << endl;
    cerr << "Edit region for 'hello'->'goodbye' would be [5,10)" << endl;

    for (int col = 0; col <= 5; col++) {
      auto r = oracle_->simulate(source, 0, col, "ci\"goodbye<Esc>");
      cerr << "  ci\" at col " << col << ": '" << r.lines[0] << "'" << endl;
    }
  }

  // Test 3: What happens with quotes when there's an earlier quote?
  cerr << endl << "=== ci\" with earlier quote on line ===" << endl;
  {
    Lines source = {"a\"b \"hello\" bar"};
    cerr << "Source: '" << source[0] << "'" << endl;
    cerr << "Quotes at cols 1, 4, 10" << endl;

    auto r0 = oracle_->simulate(source, 0, 0, "ci\"goodbye<Esc>");
    cerr << "  ci\" at col 0: '" << r0.lines[0] << "'" << endl;

    auto r3 = oracle_->simulate(source, 0, 3, "ci\"goodbye<Esc>");
    cerr << "  ci\" at col 3: '" << r3.lines[0] << "'" << endl;

    auto r5 = oracle_->simulate(source, 0, 5, "ci\"goodbye<Esc>");
    cerr << "  ci\" at col 5: '" << r5.lines[0] << "'" << endl;
  }

  // Test 4: Simple quote case
  cerr << endl << "=== Simple quote case ===" << endl;
  {
    Lines source = {"\"hello\""};
    cerr << "Source: '" << source[0] << "'" << endl;

    auto r0 = oracle_->simulate(source, 0, 0, "ci\"goodbye<Esc>");
    cerr << "  ci\" at col 0 (on quote): '" << r0.lines[0] << "'" << endl;

    auto r1 = oracle_->simulate(source, 0, 1, "ci\"goodbye<Esc>");
    cerr << "  ci\" at col 1 (on 'h'): '" << r1.lines[0] << "'" << endl;
  }

  // Test 5: Check what DiffState produces for the quote test case
  cerr << endl << "=== DiffState for quote test ===" << endl;
  {
    Lines initial = {"foo \"hello\" bar"};
    Lines goal = {"foo \"goodbye\" bar"};

    auto diffs = Myers::calculate(initial, goal);
    cerr << "Diffs: " << diffs.size() << endl;
    for (size_t i = 0; i < diffs.size(); i++) {
      cerr << "  Diff " << i << ": deleted='" << diffs[i].deletedText
           << "' inserted='" << diffs[i].insertedText << "'" << endl;
      cerr << "    beginPos=(" << diffs[i].beginPos.line << "," << diffs[i].beginPos.col
           << ") endPos=(" << diffs[i].endPos.line << "," << diffs[i].endPos.col << ")" << endl;
    }
  }

  // Test 6: Run the composition optimizer and see what it produces
  cerr << endl << "=== CompositionOptimizer for quote test ===" << endl;
  {
    Lines initial = {"foo \"hello\" bar"};
    Lines goal = {"foo \"goodbye\" bar"};
    Position initialPos(0, 0);

    Config config = Config::uniform();
    CompositionOptimizer opt{config};
    CompositionOptimizerParams params = CompositionOptimizerParams{}.withMaxResults(10);

    vector<Result> results = opt.optimize(
        initial, initialPos, goal, Position(0,0), params);

    cerr << "Results: " << results.size() << endl;
    for (size_t i = 0; i < results.size(); i++) {
      const auto& seq = results[i].sequence;
      cerr << "  " << i << ": '" << seq << "' cost=" << results[i].keyCost << endl;

      auto nvim = oracle_->simulate(initial, 0, 0, seq.keys);
      bool correct = (nvim.lines == goal);
      cerr << "      -> '" << nvim.lines[0] << "' " << (correct ? "OK" : "WRONG") << endl;
    }
  }
}

TEST_F(NeovimOracleDebug, InvestigateMaskBugs) {
  Config config = Config::uniform();
  CompositionOptimizerParams params{};

  auto makeCtx = [&](const Lines& initial, const Lines& goal) {
    return CompositionSearchContext(
        initial, Position(0, 0), goal, "",
        NavContext(), MotionBoundary(), EXPLORABLE_MOTIONS, params, config);
  };

  // Bug 1: Bracket mask doesn't mark positions INSIDE the brackets
  cerr << "\n=== Bug 1: Bracket positions inside pair ===" << endl;
  {
    Lines initial = {"foo (hello) bar"};
    Lines goal = {"foo (X) bar"};
    auto ctx = makeCtx(initial, goal);
    const auto& toCtx = ctx.bracketQuoteContexts[0];
    const auto& diff = ctx.diffStates[0];

    cerr << "Line: '" << initial[0] << "'" << endl;
    cerr << "Diff: deleted='" << diff.deletedText << "' inserted='" << diff.insertedText
         << "' begin=(" << diff.beginPos.line << "," << diff.beginPos.col
         << ") end=(" << diff.endPos.line << "," << diff.endPos.col << ")" << endl;
    cerr << "toCtx.line=" << toCtx.line << endl;

    cerr << "Bracket mask for '(':" << endl;
    for (int col = 0; col < static_cast<int>(initial[0].size()); col++) {
      bool maskValid = col < static_cast<int>(toCtx.validBracketMask.size())
                       && toCtx.validBracketMask[col].seen('(');
      auto r = oracle_->simulate(initial, 0, col, "ci(X<Esc>");
      bool oracleValid = (r.lines == goal);
      cerr << "  col " << col << ": mask=" << maskValid << " oracle=" << oracleValid
           << (maskValid != oracleValid ? " MISMATCH" : "") << endl;
    }
  }

  // Bug 2: Quote mask for second pair
  cerr << "\n=== Bug 2: Quote second pair ===" << endl;
  {
    Lines initial = {"aaa \"first\" bbb \"second\" ccc"};
    Lines goal = {"aaa \"first\" bbb \"X\" ccc"};
    auto ctx = makeCtx(initial, goal);
    const auto& toCtx = ctx.bracketQuoteContexts[0];
    const auto& diff = ctx.diffStates[0];

    cerr << "Line: '" << initial[0] << "'" << endl;
    cerr << "Diff: deleted='" << diff.deletedText << "' inserted='" << diff.insertedText
         << "' begin=(" << diff.beginPos.line << "," << diff.beginPos.col
         << ") end=(" << diff.endPos.line << "," << diff.endPos.col << ")" << endl;

    cerr << "Quote mask for '\"':" << endl;
    for (int col = 0; col < static_cast<int>(initial[0].size()); col++) {
      bool maskValid = col < static_cast<int>(toCtx.validQuoteMask.size())
                       && toCtx.validQuoteMask[col].seen('"');
      auto r = oracle_->simulate(initial, 0, col, "ci\"X<Esc>");
      bool oracleValid = (r.lines == goal);
      char ch = initial[0][col];
      cerr << "  col " << col << " '" << ch << "': mask=" << maskValid
           << " oracle=" << oracleValid
           << (maskValid != oracleValid ? " MISMATCH" : "") << endl;
    }
  }

  // Bug 3: Nested brackets - inner pair
  cerr << "\n=== Bug 3: Nested brackets (inner) ===" << endl;
  {
    Lines initial = {"a ((hello)) c"};
    Lines goal = {"a ((X)) c"};
    auto ctx = makeCtx(initial, goal);
    const auto& toCtx = ctx.bracketQuoteContexts[0];
    const auto& diff = ctx.diffStates[0];

    cerr << "Line: '" << initial[0] << "'" << endl;
    cerr << "Diff: deleted='" << diff.deletedText << "' inserted='" << diff.insertedText
         << "' begin=(" << diff.beginPos.line << "," << diff.beginPos.col
         << ") end=(" << diff.endPos.line << "," << diff.endPos.col << ")" << endl;

    cerr << "Bracket mask for '(':" << endl;
    for (int col = 0; col < static_cast<int>(initial[0].size()); col++) {
      bool maskValid = col < static_cast<int>(toCtx.validBracketMask.size())
                       && toCtx.validBracketMask[col].seen('(');
      auto r = oracle_->simulate(initial, 0, col, "ci(X<Esc>");
      bool oracleValid = (r.lines == goal);
      char ch = initial[0][col];
      cerr << "  col " << col << " '" << ch << "': mask=" << maskValid
           << " oracle=" << oracleValid
           << (maskValid != oracleValid ? " MISMATCH" : "") << endl;
    }
  }
}

TEST_F(NeovimOracleDebug, DISABLED_InvestigateCompositionOptimizer) {
  cerr << "=== CompositionOptimizer debug ===" << endl;

  Lines initial = {"hello world"};
  Lines goal = {"hello there"};
  Position initialPos(0, 0);
  Position goalPos(0, 0);

  cerr << "Initial: " << initial << endl;
  cerr << "Goal: " << goal << endl;
  cerr << "Start position: " << initialPos << endl;

  // First, check what diffs are computed
  auto diffs = Myers::calculate(initial, goal);
  cerr << endl << "Diffs computed: " << diffs.size() << endl;
  for (size_t i = 0; i < diffs.size(); i++) {
    cerr << "  Diff " << i << ": '" << diffs[i].deletedText << "' -> '" << diffs[i].insertedText << "'" << endl;
    cerr << "    beginPos: " << diffs[i].beginPos << ", endPos: " << diffs[i].endPos << endl;
  }

  // Test MotionOptimizer.optimizeToRange directly
  cerr << endl << "=== Testing MotionOptimizer.optimizeToRange ===" << endl;
  {
    Config cfg = Config::uniform();
    MotionOptimizer movOpt(cfg);
    Position rangeFirst(0, 6);
    Position rangeEnd(0, 11);

    cerr << "Finding path from " << initialPos << " to range [" << rangeFirst << ", " << rangeEnd << ")" << endl;

    auto rangeResult = movOpt.optimizeToRange(
        initial, initialPos, rangeFirst, rangeEnd,
        MotionOptimizerRangeParams{}.withMaxResults(10));

    cerr << "MotionOptimizer returned " << rangeResult.results.size() << " results" << endl;
    cerr << "Stats: nodes=" << rangeResult.stats.nodesExplored
         << " stopReason=" << static_cast<int>(rangeResult.stats.stopReason) << endl;

    for (size_t i = 0; i < rangeResult.results.size() && i < 5; i++) {
      const auto& r = rangeResult.results[i];
      cerr << "  Motion " << i << ": '" << r.getSequenceString() << "' -> " << r.goalPos
           << " cost=" << r.keyCost << endl;
    }
  }

  // Now run the full optimizer
  Config config = Config::uniform();
  CompositionOptimizer opt{config};
  CompositionOptimizerParams params{};

  cerr << endl << "Running CompositionOptimizer..." << endl;
  vector<Result> results = opt.optimize(
      initial, initialPos, goal, goalPos, params);

  cerr << "Results: " << results.size() << endl;
  for (size_t i = 0; i < results.size(); i++) {
    cerr << "  Result " << i << ": '" << results[i].sequence << "' cost=" << results[i].keyCost << endl;
  }

  if (!results.empty()) {
    const auto& seq = results[0].sequence;
    auto nvim = oracle_->simulate(initial, initialPos.line, initialPos.col, seq.keys);
    cerr << endl << "Neovim result for '" << seq << "':" << endl;
    cerr << "  Lines: " << nvim.lines << endl;
    cerr << "  Goal:  " << goal << endl;
    cerr << "  Match: " << (nvim.lines == goal ? "YES" : "NO") << endl;
  }
}

// =============================================================================
// InsertNewLine iter=27: ciw mismatch investigation
// =============================================================================

TEST_F(NeovimOracleDebug, InvestigateCiwMismatch) {
  // FAIL iter=27 seq='jciwcba<CR>b<Esc>'
  //   Initial: bba  c / b b,
  //   Goal:    bba  c / cba / b b,
  //   Got:     bba  c / cba / bb,    ← space missing

  Lines initial = {"bba  c", "b b, "};
  Position initialPos(0, 0);

  cerr << "=== Neovim step-by-step ===" << endl;
  auto tracer = makeTracer(initial, 0, 0);
  tracer.trace("j");
  tracer.trace("ciwcba<CR>b<Esc>");
  tracer.printSummary();

  // Now trace more granularly: j alone, then ciw alone
  cerr << endl << "=== Neovim: j motion only ===" << endl;
  {
    auto r = oracle_->simulate(initial, 0, 0, "j");
    cerr << "  After j from (0,0): cursor=(" << r.row << "," << r.col << ")" << endl;
    cerr << "  Buffer: " << r.lines << endl;

    // Now ciw from that position
    cerr << endl << "=== Neovim: ciw text object range check ===" << endl;
    // Use diw to see what it deletes (same range as ciw, stays in normal mode)
    auto rdiw = oracle_->simulate(initial, r.row, r.col, "diw");
    cerr << "  After diw from (" << r.row << "," << r.col << "): buffer='" << rdiw.lines[r.row] << "'" << endl;
    cerr << "  Full buffer: " << rdiw.lines << endl;
    cerr << "  Cursor: (" << rdiw.row << "," << rdiw.col << ")" << endl;
  }

  cerr << endl << "=== Our VimCore simulation ===" << endl;
  {
    Lines ourLines = initial;
    Position pos(0, 0);

    // Simulate j: move down, preserve targetCol
    pos.line = 1;
    int lastCol = ourLines[1].empty() ? 0 : static_cast<int>(ourLines[1].size()) - 1;
    pos.clampColPreservingTarget(std::min(pos.targetCol, lastCol));
    cerr << "  After j: pos=(" << pos.line << "," << pos.col
         << ") targetCol=" << pos.targetCol << endl;
    cerr << "  Char at cursor: '" << ourLines[pos.line][pos.col] << "'" << endl;

    // Simulate ciw: compute text object range
    Range iwRange = VimCore::textObject(pos, ourLines, /*isInner=*/true, /*isBigWord=*/false);
    cerr << "  textObject(iw) range: [(" << iwRange.first.line << "," << iwRange.first.col
         << "), (" << iwRange.last.line << "," << iwRange.last.col << ")]" << endl;
    cerr << "  Deleted text: '";
    for (int c = iwRange.first.col; c <= iwRange.last.col; c++) {
      cerr << ourLines[iwRange.first.line][c];
    }
    cerr << "'" << endl;

    // Apply deletion (change mode)
    VimCore::deleteRange(ourLines, iwRange, pos, Mode::Insert);
    cerr << "  After ciw deletion: buffer=" << ourLines << endl;
    cerr << "  Cursor: (" << pos.line << "," << pos.col << ")" << endl;
    cerr << "  Line content: '" << ourLines[pos.line] << "'" << endl;
  }

  // Isolate: does <CR> in insert mode strip leading whitespace?
  cerr << endl << "=== <CR> whitespace stripping test ===" << endl;
  {
    // Insert mode Enter on "cba b, " at col 3 — does it strip the leading space?
    // Use i to enter insert at col 0, type "cba", then Enter
    auto r1 = oracle_->simulate({" b, "}, 0, 0, "icba<CR><Esc>");
    cerr << "  ' b, ' -> i + type 'cba<CR>' -> " << r1.lines << endl;
    cerr << "  Line 1 content: '" << (r1.lines.size() > 1 ? r1.lines[1] : "(none)") << "'" << endl;

    // Same but no leading space
    auto r2 = oracle_->simulate({"b, "}, 0, 0, "icba<CR><Esc>");
    cerr << "  'b, ' -> i + type 'cba<CR>' -> " << r2.lines << endl;

    // Direct test: Enter in middle of line with space after cursor
    auto r3 = oracle_->simulate({"abc def"}, 0, 3, "a<CR><Esc>");
    cerr << "  'abc def' -> a at col 3 + <CR> -> " << r3.lines << endl;
    cerr << "  Line 1: '" << (r3.lines.size() > 1 ? r3.lines[1] : "(none)") << "'" << endl;

    // Enter with leading whitespace after cursor
    auto r4 = oracle_->simulate({"abc  def"}, 0, 3, "a<CR><Esc>");
    cerr << "  'abc  def' -> a at col 3 + <CR> -> " << r4.lines << endl;
    cerr << "  Line 1: '" << (r4.lines.size() > 1 ? r4.lines[1] : "(none)") << "'" << endl;
  }

  // Also verify the full sequence with <CR>/<Esc> notation
  cerr << endl << "=== Full sequence result ===" << endl;
  {
    auto r = oracle_->simulate(initial, 0, 0, "jciwcba<CR>b<Esc>");
    cerr << "  Result: " << r.lines << endl;
    cerr << "  Expected: " << Lines{"bba  c", "cba", "b b, "} << endl;
    cerr << "  Match: " << (r.lines == Lines{"bba  c", "cba", "b b, "} ? "YES" : "NO") << endl;
  }
}

// =============================================================================
// CompositionOptimizer Debug: Trace A* search for a specific failure
// =============================================================================

TEST_F(DebugTest, CompositionOptimizer_TraceFailure) {
  // Reproduce TwoEdits_DifferentLines iter=0:
  //   Initial: 'b,f,dd' / 'b,, ca..b' / 'ab ,e d..f'
  //   Goal: 'bbba' / 'b,, ca..b' / 'fbbf'
  //   Bad result: 'lcEbba<Esc> <C-d>cc<Del>fbbf<Esc>' - cc<Del> produces empty line
  Lines initial = {"b,f,dd", "b,, ca..b", "ab ,e d..f"};
  Lines goal = {"bbba", "b,, ca..b", "fbbf"};
  Position initialPos(0, 0);
  Position goalPos(0, 0);
  Config config = Config::uniform();

  // First, run the actual optimizer and print all results
  cerr << "\n========== STEP 0: Actual Optimizer Results ==========" << endl;
  {
    CompositionOptimizer opt(config);
    CompositionOptimizerParams optParams{};
    auto actualResults = opt.optimize(initial, initialPos, goal, goalPos, optParams);
    cerr << "  Total results: " << actualResults.size() << endl;
    auto oracle = make_unique<NeovimOracle>();
    for (size_t i = 0; i < actualResults.size(); i++) {
      auto nvim = oracle->simulate(initial, initialPos.line, initialPos.col, actualResults[i].sequence.keys);
      cerr << "  [" << i << "] seq='" << actualResults[i].sequence
           << "' cost=" << actualResults[i].keyCost
           << " nvim=" << (nvim.lines == goal ? "OK" : "WRONG")
           << " got=" << nvim.lines << endl;
    }
  }

  cerr << "\n========== STEP 1: Myers Diff ==========" << endl;
  auto diffs = Myers::calculate(initial, goal);
  cerr << "Number of diffs: " << diffs.size() << endl;
  for (size_t i = 0; i < diffs.size(); i++) {
    const auto& d = diffs[i];
    cerr << "  Diff " << i << ": beginPos=(" << d.beginPos.line << "," << d.beginPos.col << ")"
         << " endPos=(" << d.endPos.line << "," << d.endPos.col << ")"
         << " deleted='" << d.deletedText << "' inserted='" << d.insertedText << "'"
         << " isPure=" << (d.isPureInsertion() ? "ins" : d.isPureDeletion() ? "del" : "repl")
         << endl;
  }

  cerr << "\n========== STEP 2: EditOptimizer for each diff ==========" << endl;
  EditOptimizer editOpt(config);
  for (size_t i = 0; i < diffs.size(); i++) {
    const auto& d = diffs[i];
    if (d.isPureInsertion()) {
      cerr << "  Diff " << i << ": pure insertion, skipping EditOptimizer" << endl;
      continue;
    }
    EditResult editResult = editOpt.optimizeEdit(d.deletedLines(), d.insertedLines(), d.boundary);
    editResult.initFlatIndex(d.deletedLines(), d.beginPos.line, d.beginPos.col, d.beginPos);

    cerr << "  Diff " << i << ": EditResult has " << editResult.resultCount() << " positions" << endl;

    for (size_t j = 0; j < editResult.resultCount(); j++) {
      const auto& r = editResult.getResults()[j];
      if (r.isValid()) {
        cerr << "    pos " << j << ": seq='" << r.sequence << "' cost=" << r.keyCost << endl;
      } else {
        cerr << "    pos " << j << ": INVALID" << endl;
      }
    }

    // Test resultAt for various cursor positions
    cerr << "  resultAt tests:" << endl;
    for (int col = 0; col < static_cast<int>(initial[0].size()); col++) {
      const Result* r = editResult.resultAt(0, col);
      if (r) {
        cerr << "    col=" << col << " -> valid result" << endl;
      }
    }
  }

  cerr << "\n========== STEP 3: MotionOptimizer optimizeToRange ==========" << endl;
  {
    assert(!diffs.empty());
    const auto& d = diffs[0];
    Position rangeFirst = d.beginPos;
    Position rangeEnd = d.endPos;

    cerr << "  Range: [(" << rangeFirst.line << "," << rangeFirst.col << "), ("
         << rangeEnd.line << "," << rangeEnd.col << "))" << endl;
    cerr << "  StartPos: (" << initialPos.line << "," << initialPos.col << ")" << endl;

    MotionBoundary boundary(initial,
        Position(0, 0),
        Position(0, static_cast<int>(initial[0].size())),
        false, false);

    MotionOptimizer motionOpt(config);
    NavContext navCtx;
    MotionToKeys motionToKeys;

    auto rangeResult = motionOpt.optimizeToRange(
        initial, initialPos, rangeFirst, rangeEnd,
        MotionOptimizerRangeParams{}.withMaxResults(10), "",
        boundary, RunningEffort(), navCtx, motionToKeys);

    cerr << "  Range results: " << rangeResult.results.size() << endl;
    for (size_t i = 0; i < rangeResult.results.size(); i++) {
      const auto& r = rangeResult.results[i];
      if (r.isValid()) {
        cerr << "    [" << i << "] seq='" << r.sequence << "' cost=" << r.keyCost
             << " goalPos=(" << r.goalPos.line << "," << r.goalPos.col << ")" << endl;
      }
    }
  }

  cerr << "\n========== STEP 4: Trace A* Search ==========" << endl;
  {
    CompositionOptimizerParams params{};
    MotionOptimizer motionOpt(config);
    NavContext navCtx;
    MotionToKeys motionKeys;
    MotionBoundary boundary(initial,
        Position(0, 0),
        Position(0, static_cast<int>(initial[0].size()) - 1),
        false, false);

    CompositionSearchContext ctx(initial, initialPos, goal, "",
        navCtx, boundary, motionKeys, params, config);

    cerr << "  totalEdits=" << ctx.totalEdits << endl;
    for (int i = 0; i < ctx.totalEdits; i++) {
      const auto& d = ctx.diffStates[i];
      cerr << "  diff[" << i << "]: begin=(" << d.beginPos.line << "," << d.beginPos.col
           << ") end=(" << d.endPos.line << "," << d.endPos.col << ")" << endl;
      const auto& er = ctx.editResults[i];
      cerr << "    editResult: " << er.resultCount() << " positions, goalPos=("
           << er.goalPos.line << "," << er.goalPos.col << ")" << endl;
    }

    // Push initial state (same as CompositionOptimizer::optimize does)
    CompositionState startingState(initialPos, Mode::Normal, 0);
    startingState.setCost(ctx.heuristic(startingState, 0));
    ctx.pq.push(startingState);
    ctx.costMap[startingState.getKey()] = startingState.getCost();

    // Manual A* trace — pop states and print what happens
    int popCount = 0;
    vector<Result> results;
    while (ctx.shouldContinue() && popCount < 50) {
      CompositionState s = ctx.popNext();
      Position pos = s.getPos();
      int editsCompleted = s.getEditsCompleted();
      popCount++;

      if (ctx.isGoal(s)) {
        cerr << "  POP " << popCount << ": GOAL pos=(" << pos.line << "," << pos.col
             << ") edits=" << editsCompleted
             << " seq='" << s.getSequence() << "' effort=" << s.getEffort()
             << " cost=" << s.getCost() << endl;
        results.emplace_back(s.getMotionSequence(), s.getRunningEffort().getEffort(config));
        if (results.size() >= 3) break;
        continue;
      }

      if (ctx.isStale(s)) {
        cerr << "  POP " << popCount << ": STALE pos=(" << pos.line << "," << pos.col
             << ") edits=" << editsCompleted << " seq='" << s.getSequence() << "'" << endl;
        continue;
      }
      ctx.markProcessed();

      const Lines& currentLines = ctx.getLinesAfter(editsCompleted);
      const DiffState& nextEdit = ctx.getDiffState(editsCompleted);

      if (nextEdit.isPureInsertion()) {
        cerr << "  POP " << popCount << ": PURE_INS pos=(" << pos.line << "," << pos.col
             << ") edits=" << editsCompleted << " seq='" << s.getSequence() << "'" << endl;
        continue; // skip insertion handling for this trace
      }

      const EditResult& editResult = ctx.editResults[editsCompleted];
      const Result* editRes = editResult.resultAt(pos.line, pos.col);

      cerr << "  POP " << popCount << ": pos=(" << pos.line << "," << pos.col
           << ") edits=" << editsCompleted << " seq='" << s.getSequence()
           << "' effort=" << s.getEffort() << " cost=" << s.getCost()
           << " hasResult=" << (editRes ? "yes" : "no") << endl;

      if (editRes) {
        // Edit transition
        cerr << "    -> EDIT: seq='" << editRes->sequence << "'" << endl;
        ctx.exploreEditTransition(s, editRes->sequence,
                                  editResult.goalPos, editsCompleted + 1);
      } else {
        // Motion search
        auto [beginLine, endLine] = currentLines.minmaxBoundWithPadding(
            min(pos.line, nextEdit.beginPos.line),
            max(pos.line, nextEdit.endPos.line),
            params.motionPaddingAbove, params.motionPaddingBelow);

        Lines subset = currentLines.getLineRange(beginLine, endLine);
        Position localPos(pos.line - beginLine, pos.col, pos.targetCol);
        Position localRangeFirst(nextEdit.beginPos.line - beginLine, nextEdit.beginPos.col);
        Position localRangeEnd(nextEdit.endPos.line - beginLine, nextEdit.endPos.col);

        Position subsetEnd(static_cast<int>(subset.size()) - 1,
            subset.back().effectiveSize());
        MotionBoundary subsetBoundary(subset, localRangeFirst, subsetEnd,
            beginLine > 0 || boundary.hasLinesAbove(),
            endLine <= currentLines.lastLine() || boundary.hasLinesBelow());

        auto movementResults = motionOpt.optimizeToRange(
            subset, localPos, localRangeFirst, localRangeEnd,
            MotionOptimizerRangeParams{}.withMaxResults(
                clamp(nextEdit.origCharCount(), 1, 10)), "",
            subsetBoundary, s.getRunningEffort(), navCtx, ctx.motionToKeys).results;

        for (auto& movResult : movementResults) {
          if (!movResult.isValid()) continue;
          movResult.goalPos.line += beginLine;
          cerr << "    -> MOTION: seq='" << movResult.sequence << "' goalPos=("
               << movResult.goalPos.line << "," << movResult.goalPos.col << ")" << endl;
          ctx.exploreMotionTransition(s, movResult.sequence, movResult.goalPos, editsCompleted);
        }
      }
    }

    cerr << "\nFinal results: " << results.size() << endl;
    auto oracle = make_unique<NeovimOracle>();
    for (size_t i = 0; i < results.size(); i++) {
      auto nvim = oracle->simulate(initial, initialPos.line, initialPos.col, results[i].sequence.keys);
      cerr << "  [" << i << "] seq='" << results[i].sequence << "' cost=" << results[i].keyCost
           << " nvim=" << (nvim.lines == goal ? "OK" : "WRONG") << " got=" << nvim.lines << endl;
    }
  }
}
