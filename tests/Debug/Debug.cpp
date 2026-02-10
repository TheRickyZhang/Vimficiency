// tests/Debug/Debug.cpp
//
// Debug utilities and scratch tests for development.
// Enable a test by removing DISABLED_ prefix.
//
// Run: ./build/tests/vimficiency_debug --gtest_filter="DebugTest.*"
//   - Or: ./vimficiency_tests --gtest_filter="NeovimOracleDebug.*"

#include <gtest/gtest.h>

#include "Editor/Edit.h"
#include "Optimizer/Config.h"
#include "Optimizer/EditOptimizer/EditOptimizer.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Optimizer/CompositionOptimizer/CompositionSearchContext.h"
#include "Optimizer/CompositionOptimizer/DiffState.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"
#include "Boundary/EditBoundary.h"
#include "Boundary/MotionBoundary.h"
#include "Utils/EditTestGenerators.h"
#include "Utils/NeovimOracle.h"
#include "Utils/StringUtils.h"
#include "State/EditState.h"
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
  auto oracle = make_unique<NeovimOracle>();

  // Reproduce TwoEdits_SameLine iter=12
  cerr << "=== TwoEdits_SameLine iter=12 ===" << endl;
  {
    Config config = Config::uniform();
    CompositionOptimizer opt{config};
    CompositionOptimizerParams params = CompositionOptimizerParams{}.withMaxResults(5);

    // Reproduce iter=12 from the test (seed=48)
    Lines initial = {"ffb decd bdf"};
    Lines goal = {"cbb decd fed"};
    Position initialPos(0, 0);

    auto compResult = opt.optimize(initial, initialPos, goal, Position(0, 0), params);
    const auto& results = compResult.results;

    cerr << "Results: " << results.size() << endl;
    for (size_t i = 0; i < results.size(); i++) {
      const auto& seq = results[i].sequence;
      // Print sequence bytes
      cerr << "  [" << i << "] seq='" << seq << "' (len=" << seq.keys.size() << ")" << endl;
      cerr << "       bytes: ";
      for (char c : seq.keys) cerr << static_cast<int>(static_cast<unsigned char>(c)) << " ";
      cerr << endl;
      cerr << "       cost=" << results[i].keyCost << endl;

      auto nvim = oracle->simulate(initial, 0, 0, seq.keys);
      cerr << "       nvim: " << nvim.lines << (nvim.lines == goal ? " OK" : " WRONG") << endl;
    }
  }

  // Also trace step by step what the edit optimizer produces for each diff
  cerr << "\n=== Diff regions ===" << endl;
  {
    Lines initial = {"ffb decd bdf"};
    Lines goal = {"cbb decd fed"};
    auto diffs = Myers::calculate(initial, goal);
    cerr << "Diffs: " << diffs.size() << endl;
    for (size_t i = 0; i < diffs.size(); i++) {
      const auto& d = diffs[i];
      cerr << "  [" << i << "] begin=(" << d.beginPos.line << "," << d.beginPos.col
           << ") end=(" << d.endPos.line << "," << d.endPos.col << ")"
           << " del='" << d.deletedText << "' ins='" << d.insertedText << "'"
           << " prefix='" << d.boundary.prefix() << "' suffix='" << d.boundary.suffix() << "'"
           << endl;
    }

    // Edit optimizer for each diff
    Config config = Config::uniform();
    EditOptimizer editOpt(config);
    for (size_t i = 0; i < diffs.size(); i++) {
      const auto& d = diffs[i];
      if (d.isPureInsertion()) continue;
      EditResult result = editOpt.optimizeEdit(
          d.deletedLines(), d.insertedLines(), d.boundary, {},
          d.beginPos.line, d.beginPos.col, d.beginPos);
      cerr << "  Edit[" << i << "] goalPos=(" << result.goalPos.line << "," << result.goalPos.col
           << ") results:" << endl;
      for (size_t j = 0; j < result.resultCount(); j++) {
        if (result.getResults()[j].isValid()) {
          cerr << "    pos " << j << ": '" << result.getResults()[j].sequence
               << "' cost=" << result.getResults()[j].keyCost << endl;
        }
      }
    }
  }

  // SingleLine_Substitution iter=0
  cerr << "\n=== SingleLine_Substitution iter=0 ===" << endl;
  {
    Config config = Config::uniform();
    CompositionOptimizer opt{config};
    CompositionOptimizerParams params = CompositionOptimizerParams{}.withMaxResults(5);

    Lines initial = {"efbeeddacaaa"};
    Lines goal = {"efbedaeaaa"};

    auto compResult = opt.optimize(initial, Position(0,0), goal, Position(0,0), params);
    cerr << "Results: " << compResult.results.size() << endl;
    for (size_t i = 0; i < compResult.results.size(); i++) {
      cerr << "  [" << i << "] '" << compResult.results[i].sequence
           << "' cost=" << compResult.results[i].keyCost << endl;
    }

    auto diffs = Myers::calculate(initial, goal);
    cerr << "Diffs: " << diffs.size() << endl;
    for (size_t i = 0; i < diffs.size(); i++) {
      const auto& d = diffs[i];
      cerr << "  [" << i << "] begin=(" << d.beginPos.line << "," << d.beginPos.col
           << ") end=(" << d.endPos.line << "," << d.endPos.col << ")"
           << " del='" << d.deletedText << "' ins='" << d.insertedText << "'"
           << " prefix='" << d.boundary.prefix() << "' suffix='" << d.boundary.suffix() << "'"
           << endl;

      if (!d.isPureInsertion()) {
        EditOptimizer editOpt(config);
        EditResult result = editOpt.optimizeEdit(
            d.deletedLines(), d.insertedLines(), d.boundary, {},
            d.beginPos.line, d.beginPos.col, d.beginPos);
        int validCount = 0;
        for (size_t j = 0; j < result.resultCount(); j++) {
          if (result.getResults()[j].isValid()) validCount++;
        }
        cerr << "    Edit valid: " << validCount << "/" << result.resultCount()
             << " nodes=" << result.stats.nodesExplored << endl;
        for (size_t j = 0; j < result.resultCount(); j++) {
          if (result.getResults()[j].isValid()) {
            cerr << "    pos " << j << ": '" << result.getResults()[j].sequence
                 << "' cost=" << result.getResults()[j].keyCost << endl;
          }
        }
      }
    }
  }

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

    auto compResult = opt.optimize(
        initial, initialPos, goal, Position(0,0), params);
    const auto& results = compResult.results;

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
        NavContext(), MotionBoundary(), params, config);
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
  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.results;

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
    auto compResult = opt.optimize(initial, initialPos, goal, goalPos, optParams);
    const auto& actualResults = compResult.results;
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
    EditResult editResult = editOpt.optimizeEdit(
        d.deletedLines(), d.insertedLines(), d.boundary, {},
        d.beginPos.line, d.beginPos.col, d.beginPos);

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

    auto rangeResult = motionOpt.optimizeToRange(
        initial, initialPos, rangeFirst, rangeEnd,
        MotionOptimizerRangeParams{}.withMaxResults(10), "",
        boundary, RunningEffort(), navCtx);

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
    MotionBoundary boundary(initial,
        Position(0, 0),
        Position(0, static_cast<int>(initial[0].size()) - 1),
        false, false);

    CompositionSearchContext ctx(initial, initialPos, goal, "",
        navCtx, boundary, params, config);

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
            subsetBoundary, s.getRunningEffort(), navCtx).results;

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

// =============================================================================
// HumanApproval Example1: `i` instead of `ce`/`cw` for replacement edits
// =============================================================================

TEST_F(DebugTest, InvestigateTelescopingSearch) {
  Lines initial = {"Today I saw a giraffe in museum in Switzerland",
                    "Inconspicuous, even"};
  Lines goal = {"I saw a pig in barn in Florida"};
  Position initialPos(0, 0);

  CompositionOptimizerParams compParams{};

  // Step 1: Diffs and intermediate buffers
  cerr << "\n=== Step 1: Diffs ===" << endl;
  CompositionSearchContext ctx(initial, initialPos, goal, "",
      NavContext(), MotionBoundary(), compParams, config);
  cerr << "totalEdits=" << ctx.totalEdits << endl;
  for (int i = 0; i < ctx.totalEdits; i++) {
    const auto& d = ctx.diffStates[i];
    cerr << "  diff[" << i << "] begin=(" << d.beginPos.line << "," << d.beginPos.col
         << ") end=(" << d.endPos.line << "," << d.endPos.col << ")"
         << " del='" << makePrintable(d.deletedText) << "'"
         << " ins='" << makePrintable(d.insertedText) << "'"
         << " type=" << (d.isPureInsertion() ? "INSERT" : d.isPureDeletion() ? "DELETE" : "REPLACE")
         << endl;
    cerr << "    buffer[" << i << "]: " << ctx.linesAfterNEdits[i] << endl;
  }

  // Step 2: Edit results for each diff
  cerr << "\n=== Step 2: EditResults per diff ===" << endl;
  for (int i = 0; i < ctx.totalEdits; i++) {
    const auto& er = ctx.editResults[i];
    const auto& d = ctx.diffStates[i];
    cerr << "  edit[" << i << "] goalPos=(" << er.goalPos.line << "," << er.goalPos.col
         << ") resultCount=" << er.resultCount() << endl;

    // Show valid results at each position in the edit region
    int validCount = 0;
    for (size_t j = 0; j < er.getResults().size(); j++) {
      if (er.getResults()[j].isValid()) {
        validCount++;
        if (validCount <= 5) {
          cerr << "    pos " << j << ": '" << er.getResults()[j].sequence << "' cost="
               << er.getResults()[j].keyCost << endl;
        }
      }
    }
    cerr << "    total valid: " << validCount << " / " << er.resultCount() << endl;

    // Specifically check positions that should have results
    const auto& buf = ctx.linesAfterNEdits[i];
    for (int line = d.beginPos.line; line <= min(d.endPos.line, static_cast<int>(buf.size()) - 1); line++) {
      int startCol = (line == d.beginPos.line) ? d.beginPos.col : 0;
      int endCol = (line == d.endPos.line) ? d.endPos.col : static_cast<int>(buf[line].size());
      for (int col = startCol; col < endCol; col++) {
        const Result* r = er.resultAt(line, col);
        if (r) {
          cerr << "    resultAt(" << line << "," << col << "): '"
               << r->sequence << "' cost=" << r->keyCost << endl;
        }
      }
    }
  }

  // Step 3: A* search trace
  cerr << "\n=== Step 3: A* Search Trace ===" << endl;
  MotionOptimizer motionOpt(config);
  NavContext navCtx;
  MotionBoundary boundary;

  CompositionState startingState(initialPos, Mode::Normal, 0);
  startingState.setCost(ctx.heuristic(startingState, 0));
  ctx.pq.push(startingState);
  ctx.costMap[startingState.getKey()] = startingState.getCost();

  int popCount = 0;
  vector<Result> results;
  while (ctx.shouldContinue() && popCount < 100) {
    CompositionState s = ctx.popNext();
    Position pos = s.getPos();
    int editsCompleted = s.getEditsCompleted();
    popCount++;

    if (ctx.isGoal(s)) {
      cerr << "  POP " << popCount << ": GOAL seq='" << s.getSequence()
           << "' effort=" << s.getEffort() << " cost=" << s.getCost() << endl;
      results.emplace_back(s.getMotionSequence(), s.getRunningEffort().getEffort(config));
      if (results.size() >= 5) break;
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

    cerr << "  POP " << popCount << ": pos=(" << pos.line << "," << pos.col
         << ") edits=" << editsCompleted << " seq='" << s.getSequence()
         << "' effort=" << s.getEffort() << " cost=" << s.getCost() << endl;

    // Pure insertion handling
    if (nextEdit.isPureInsertion()) {
      cerr << "    -> PURE_INSERTION at (" << nextEdit.beginPos.line << "," << nextEdit.beginPos.col << ")" << endl;
      // Let the real optimizer handle this; just note it
      continue;
    }

    // Edit transition
    const EditResult& editResult = ctx.editResults[editsCompleted];
    const Result* res = editResult.resultAt(pos.line, pos.col);

    if (res) {
      cerr << "    -> EDIT: '" << res->sequence << "' cost=" << res->keyCost
           << " -> goalPos=(" << editResult.goalPos.line << "," << editResult.goalPos.col << ")" << endl;
      ctx.exploreEditTransition(s, res->sequence, editResult.goalPos, editsCompleted + 1);
    } else {
      cerr << "    -> NO EDIT at pos, searching motions..." << endl;

      if (pos >= nextEdit.beginPos && pos < nextEdit.endPos) {
        cerr << "    -> IN RANGE but no result, skip" << endl;
        continue;
      }

      auto [beginLine, endLine] = currentLines.minmaxBoundWithPadding(
          min(pos.line, nextEdit.beginPos.line),
          max(pos.line, nextEdit.endPos.line),
          compParams.motionPaddingAbove, compParams.motionPaddingBelow);

      Lines subset = currentLines.getLineRange(beginLine, endLine);
      Position localPos(pos.line - beginLine, pos.col, pos.targetCol);
      Position localRangeFirst(nextEdit.beginPos.line - beginLine, nextEdit.beginPos.col);
      Position localRangeEnd(nextEdit.endPos.line - beginLine, nextEdit.endPos.col);

      Position subsetFirst(0, 0);
      Position subsetEnd(static_cast<int>(subset.size()) - 1,
          subset.back().effectiveSize());
      MotionBoundary subsetBoundary(subset, subsetFirst, subsetEnd,
          beginLine > 0, endLine <= currentLines.lastLine());

      auto rangeResults = motionOpt.optimizeToRange(
          subset, localPos, localRangeFirst, localRangeEnd,
          MotionOptimizerRangeParams{}.withMaxResults(
              clamp(nextEdit.origCharCount(), 1, 10)), "",
          subsetBoundary, s.getRunningEffort(), navCtx).results;

      cerr << "    -> MOTIONS found: " << rangeResults.size() << endl;
      for (auto& movResult : rangeResults) {
        if (!movResult.isValid()) continue;
        movResult.goalPos.line += beginLine;
        cerr << "      motion '" << movResult.sequence << "' -> ("
             << movResult.goalPos.line << "," << movResult.goalPos.col << ")" << endl;
        ctx.exploreMotionTransition(s, movResult.sequence, movResult.goalPos, editsCompleted);
      }
    }
  }

  cerr << "\nSearch exhausted after " << popCount << " pops, " << results.size() << " results" << endl;
  cerr << "Queue remaining: " << ctx.pq.size() << endl;

  // Verify results
  if (!results.empty()) {
    auto oracle = make_unique<NeovimOracle>();
    for (size_t i = 0; i < results.size(); i++) {
      auto nvim = oracle->simulate(initial, 0, 0, results[i].sequence.keys);
      cerr << "  [" << i << "] '" << results[i].sequence << "' "
           << (nvim.lines == goal ? "OK" : "WRONG") << endl;
    }
  }
}

TEST_F(DebugTest, DISABLED_InvestigateJoinPlan) {
  // Debug the J plan computation for various cases
  cerr << "\n=== JoinPlan Debug ===" << endl;

  auto dumpJoinPlan = [&](const string& label, const Lines& initial, const Lines& goal,
                          Position initialPos) {
    cerr << "\n--- " << label << " ---" << endl;
    cerr << "Initial: " << initial;
    cerr << "Goal:    " << goal;

    // Step 1: Myers diffs
    auto diffs = Myers::calculate(initial, goal);
    cerr << "Diffs: " << diffs.size() << endl;
    for (size_t i = 0; i < diffs.size(); i++) {
      const auto& d = diffs[i];
      cerr << "  [" << i << "] begin=(" << d.beginPos.line << "," << d.beginPos.col
           << ") end=(" << d.endPos.line << "," << d.endPos.col << ")"
           << " del='" << makePrintable(d.deletedText) << "'"
           << " ins='" << makePrintable(d.insertedText) << "'"
           << endl;
      cerr << "    deletedLines: " << d.deletedLines();
      cerr << "    insertedLines: " << d.insertedLines();
      cerr << "    boundary: prefix='" << d.boundary.prefix() << "' suffix='" << d.boundary.suffix() << "'" << endl;
    }

    // Step 2: CompositionSearchContext (triggers computeJoinPlans)
    CompositionOptimizerParams compParams{};
    CompositionSearchContext ctx(initial, initialPos, goal, "",
        NavContext(), MotionBoundary(), compParams, config);
    cerr << "totalEdits=" << ctx.totalEdits << endl;

    for (int i = 0; i < ctx.totalEdits; i++) {
      const auto& d = ctx.diffStates[i];
      cerr << "  diff[" << i << "] begin=(" << d.beginPos.line << "," << d.beginPos.col
           << ") end=(" << d.endPos.line << "," << d.endPos.col << ")"
           << " del='" << makePrintable(d.deletedText) << "'"
           << " ins='" << makePrintable(d.insertedText) << "'" << endl;
      cerr << "    buffer[" << i << "]: " << ctx.linesAfterNEdits[i];

      if (ctx.joinPlans[i]) {
        cerr << "    JOIN PLAN: seq='" << ctx.joinPlans[i]->sequence.keys
             << "' effort=" << ctx.joinPlans[i]->effort
             << " entryLine=" << ctx.joinPlans[i]->entryLine
             << " goalPos=(" << ctx.joinPlans[i]->goalPos.line
             << "," << ctx.joinPlans[i]->goalPos.col << ")" << endl;
      } else {
        cerr << "    JOIN PLAN: none" << endl;
      }
    }

    // Step 3: Full optimizer
    CompositionOptimizer opt{config};
    auto compResult = opt.optimize(initial, initialPos, goal, goal.lastPos(), compParams);
    cerr << "Results: " << compResult.results.size() << endl;
    for (size_t i = 0; i < compResult.results.size(); i++) {
      cerr << "  [" << i << "] '" << compResult.results[i].sequence
           << "' cost=" << compResult.results[i].keyCost << endl;
    }
  };

  // Case 1: JoinLinesExact — "hello\nworld" → "hello world"
  dumpJoinPlan("JoinLinesExact",
      {"hello", "world"}, {"hello world"}, Position(0, 0));

  // Case 2: JoinLinesWithResidual — "aaa\nxxx\nccc" → "aaa bbb ccc"
  dumpJoinPlan("JoinLinesWithResidual",
      {"aaa", "xxx", "ccc"}, {"aaa bbb ccc"}, Position(0, 0));

  // Case 3: JoinLinesPartialJoin — 4 lines → 2 lines
  dumpJoinPlan("JoinLinesPartialJoin",
      {"aaa", "bbb", "ccc", "ddd"}, {"aaa bbb", "ccc ddd"}, Position(0, 0));

  // Case 3b: Debug motionOptimizer for PartialJoin
  cerr << "\n--- PartialJoin MotionOptimizer debug ---" << endl;
  {
    Lines buffer = {"aaa bbb", "ccc", "ddd"};
    Position pos(0, 3);
    Position rangeFirst(1, 3);
    Position rangeEnd(2, 0);
    MotionBoundary boundary(buffer, Position(0, 0), buffer.endPos());

    MotionOptimizer motionOpt(config);
    NavContext navCtx;
    auto rangeResult = motionOpt.optimizeToRange(
        buffer, pos, rangeFirst, rangeEnd,
        MotionOptimizerRangeParams{}.withMaxResults(5), "",
        boundary, RunningEffort(), navCtx);

    cerr << "Motion results: " << rangeResult.results.size() << endl;
    for (size_t i = 0; i < rangeResult.results.size(); i++) {
      if (rangeResult.results[i].isValid()) {
        cerr << "  [" << i << "] '" << rangeResult.results[i].sequence
             << "' -> (" << rangeResult.results[i].goalPos.line << ","
             << rangeResult.results[i].goalPos.col << ")" << endl;
      }
    }
  }
}

TEST_F(DebugTest, DISABLED_InvestigateJoinLines) {
  Lines initial = {"aaa", "bbb", "ccc"};
  Lines goal = {"aaa bbb ccc?"};
  Position initialPos(0, 2);

  // Step 1: Myers diffs
  cerr << "\n=== Myers Diffs ===" << endl;
  auto diffs = Myers::calculate(initial, goal);
  for (size_t i = 0; i < diffs.size(); i++) {
    const auto& d = diffs[i];
    cerr << "  [" << i << "] begin=(" << d.beginPos.line << "," << d.beginPos.col
         << ") end=(" << d.endPos.line << "," << d.endPos.col << ")"
         << " del='" << makePrintable(d.deletedText) << "'"
         << " ins='" << makePrintable(d.insertedText) << "'"
         << " type=" << (d.isPureInsertion() ? "INSERT" : d.isPureDeletion() ? "DELETE" : "REPLACE")
         << endl;
    cerr << "    boundary: prefix='" << d.boundary.prefix() << "' suffix='" << d.boundary.suffix() << "'"
         << " linesAbove=" << d.boundary.hasLinesAbove()
         << " linesBelow=" << d.boundary.hasLinesBelow() << endl;
    cerr << "    deletedLines: " << d.deletedLines() << endl;
    cerr << "    insertedLines: " << d.insertedLines() << endl;
  }

  // Step 2: CompositionSearchContext (tests calculateLinesAfterDiffs + calculateEditResults)
  cerr << "\n=== CompositionSearchContext ===" << endl;
  CompositionOptimizerParams compParams{};
  CompositionSearchContext ctx(initial, initialPos, goal, "",
      NavContext(), MotionBoundary(), compParams, config);
  cerr << "totalEdits=" << ctx.totalEdits << endl;
  for (int i = 0; i < ctx.totalEdits; i++) {
    const auto& d = ctx.diffStates[i];
    cerr << "  [" << i << "] begin=(" << d.beginPos.line << "," << d.beginPos.col
         << ") end=(" << d.endPos.line << "," << d.endPos.col << ")"
         << " del='" << makePrintable(d.deletedText) << "'"
         << " ins='" << makePrintable(d.insertedText) << "'"
         << " type=" << (d.isPureInsertion() ? "INSERT" : d.isPureDeletion() ? "DELETE" : "REPLACE")
         << endl;
    cerr << "    buffer[" << i << "]: " << ctx.linesAfterNEdits[i] << endl;
    cerr << "    boundary: prefix='" << d.boundary.prefix() << "' suffix='" << d.boundary.suffix() << "'"
         << " linesAbove=" << d.boundary.hasLinesAbove()
         << " linesBelow=" << d.boundary.hasLinesBelow() << endl;
  }
  cerr << "  goalBuffer: " << ctx.linesAfterNEdits[ctx.totalEdits] << endl;

  // Step 3: Try each edit independently through EditOptimizer
  cerr << "\n=== EditOptimizer per diff ===" << endl;
  EditOptimizer editOpt(config);
  for (int i = 0; i < ctx.totalEdits; i++) {
    const auto& d = ctx.diffStates[i];
    if (d.isPureInsertion()) {
      cerr << "  diff[" << i << "]: pure insertion, skip" << endl;
      continue;
    }
    cerr << "  diff[" << i << "]: calling optimizeEdit..." << endl;
    cerr << "    deletedLines: " << d.deletedLines() << endl;
    cerr << "    insertedLines: " << d.insertedLines() << endl;
    cerr << "    boundary prefix='" << d.boundary.prefix() << "' suffix='" << d.boundary.suffix() << "'" << endl;
    cerr << "    lineBase=" << d.beginPos.line << " colBase=" << d.beginPos.col << endl;

    EditResult result = editOpt.optimizeEdit(
        d.deletedLines(), d.insertedLines(), d.boundary, {},
        d.beginPos.line, d.beginPos.col, d.beginPos);

    cerr << "    -> results: " << result.stats.resultsFound
         << " nodes: " << result.stats.nodesExplored << endl;
    for (size_t j = 0; j < result.resultCount(); j++) {
      if (result.getResults()[j].isValid()) {
        cerr << "    [" << j << "] '" << result.getResults()[j].sequence
             << "' cost=" << result.getResults()[j].keyCost << endl;
      }
    }
  }

  // Step 4: Show what upstream fix would produce (stripped empty first line)
  cerr << "\n=== Upstream fix comparison ===" << endl;
  {
    const auto& d = ctx.diffStates[0];
    Lines deleted = d.deletedLines();
    Lines inserted = d.insertedLines();
    cerr << "  Original: deletedLines=" << deleted << " → insertedLines=" << inserted << endl;

    if (deleted.size() > 1 && deleted[0].empty() && !d.boundary.prefix().empty()) {
      deleted.erase(deleted.begin());
      cerr << "  After strip: deletedLines=" << deleted << " → insertedLines=" << inserted << endl;
      cerr << "  Edit region now starts at (1,0), no prefix" << endl;
      cerr << "  Buffer before edit: " << ctx.linesAfterNEdits[0] << endl;

      // If EditOptimizer transforms ["bbb","ccc"] → [" bbb ccc?"],
      // what does the buffer look like?
      Lines beforeEdit = ctx.linesAfterNEdits[0];
      // The edit replaces lines 1-2 content with the single line " bbb ccc?"
      // But the \n between line 0 and line 1 is preserved!
      cerr << "  After edit: [\"" << beforeEdit[0] << "\", \" bbb ccc?\"]" << endl;
      cerr << "  Expected:   [\"aaa bbb ccc?\"]" << endl;
      cerr << "  MISMATCH: upstream fix preserves \\n between prefix line and edit region!" << endl;
    }
  }
}

TEST_F(DebugTest, InvestigateHumanApproval1) {
  Lines initial = {"steak is pretty nice", "don't you think?"};
  Lines goal = {"Dry-brined steak is excellent", "don't you agree?"};

  // Step 1: Raw Myers diffs
  cerr << "\n=== Raw Myers Diffs ===" << endl;
  auto diffs = Myers::calculate(initial, goal);
  for (size_t i = 0; i < diffs.size(); i++) {
    const auto& d = diffs[i];
    cerr << "  [" << i << "] begin=(" << d.beginPos.line << "," << d.beginPos.col
         << ") end=(" << d.endPos.line << "," << d.endPos.col << ")"
         << " del='" << d.deletedText << "' ins='" << d.insertedText << "'"
         << " type=" << (d.isPureInsertion() ? "INSERT" : d.isPureDeletion() ? "DELETE" : "REPLACE")
         << endl;
  }

  // Step 2: CompositionSearchContext (after position adjustments)
  cerr << "\n=== CompositionSearchContext ===" << endl;
  CompositionOptimizerParams compParams{};
  CompositionSearchContext ctx(initial, Position(0,0), goal, "",
      NavContext(), MotionBoundary(), compParams, config);
  cerr << "totalEdits=" << ctx.totalEdits << endl;
  for (int i = 0; i < ctx.totalEdits; i++) {
    const auto& d = ctx.diffStates[i];
    cerr << "  [" << i << "] begin=(" << d.beginPos.line << "," << d.beginPos.col
         << ") end=(" << d.endPos.line << "," << d.endPos.col << ")"
         << " del='" << d.deletedText << "' ins='" << d.insertedText << "'"
         << " type=" << (d.isPureInsertion() ? "INSERT" : d.isPureDeletion() ? "DELETE" : "REPLACE")
         << endl;
    cerr << "    buffer[" << i << "]: " << ctx.linesAfterNEdits[i] << endl;
  }

  // Step 3: Full optimizer results with oracle verification
  cerr << "\n=== Optimizer Results ===" << endl;
  CompositionOptimizer opt{config};
  auto compResult = opt.optimize(initial, Position(0,0), goal, Position(0,0), compParams);
  cerr << compResult;

  auto oracle = make_unique<NeovimOracle>();
  for (size_t i = 0; i < compResult.results.size(); i++) {
    const auto& seq = compResult.results[i].sequence;
    auto nvim = oracle->simulate(initial, 0, 0, seq.keys);
    bool correct = (nvim.lines == goal);
    cerr << "  [" << i << "] oracle: " << (correct ? "OK" : "WRONG")
         << " got=" << nvim.lines << endl;
  }
}

// =============================================================================
// EditOptimizer for multi-line diff: why only 1 starting position finds a result
// =============================================================================

TEST_F(DebugTest, SuffixCacheComparison) {
  // Compare standard vs suffix-cached EditOptimizer on the Switzerland -> Florida case
  Lines deletedLines = {"Switzerland", "Inconspicuous, even"};
  Lines insertedLines = {"Florida"};

  Lines bufferAtEdit = {"I saw a pig in barn in Switzerland", "Inconspicuous, even"};
  Position editBeginPos(0, 23);
  Position editEndPos(1, 19);
  EditBoundary boundary(bufferAtEdit, editBeginPos, editEndPos);

  EditOptimizer editOpt(config);

  // Standard search
  cerr << "\n=== Standard optimizeEdit ===" << endl;
  EditResult stdResult = editOpt.optimizeEdit(
      deletedLines, insertedLines, boundary, params,
      editBeginPos.line, editBeginPos.col, Position(0, 29));

  int stdValid = 0;
  for (size_t i = 0; i < stdResult.resultCount(); i++) {
    if (stdResult.getResults()[i].isValid()) stdValid++;
  }
  cerr << "  nodes=" << stdResult.stats.nodesExplored
       << " results=" << stdResult.stats.resultsFound
       << " valid=" << stdValid << "/" << stdResult.resultCount()
       << " stop=" << to_string(stdResult.stats.stopReason) << endl;
  for (size_t i = 0; i < stdResult.resultCount(); i++) {
    if (stdResult.getResults()[i].isValid()) {
      cerr << "  pos " << i << ": '" << stdResult.getResults()[i].sequence
           << "' cost=" << stdResult.getResults()[i].keyCost << endl;
    }
  }

  // Suffix-cached search
  cerr << "\n=== optimizeEdit (suffix cached) ===" << endl;
  EditResult cacheResult = editOpt.optimizeEdit(
      deletedLines, insertedLines, boundary, params,
      editBeginPos.line, editBeginPos.col, Position(0, 29));

  int cacheValid = 0;
  for (size_t i = 0; i < cacheResult.resultCount(); i++) {
    if (cacheResult.getResults()[i].isValid()) cacheValid++;
  }
  cerr << "  nodes=" << cacheResult.stats.nodesExplored
       << " results=" << cacheResult.stats.resultsFound
       << " valid=" << cacheValid << "/" << cacheResult.resultCount()
       << " stop=" << to_string(cacheResult.stats.stopReason)
       << " cacheHits=" << cacheResult.stats.cacheHits
       << " cacheEntries=" << cacheResult.stats.cacheEntries
       << " populations=" << cacheResult.stats.cachePopulations << endl;
  for (size_t i = 0; i < cacheResult.resultCount(); i++) {
    if (cacheResult.getResults()[i].isValid()) {
      cerr << "  pos " << i << ": '" << cacheResult.getResults()[i].sequence
           << "' cost=" << cacheResult.getResults()[i].keyCost << endl;
    }
  }

  // Summary
  cerr << "\n=== Summary ===" << endl;
  cerr << "Standard: " << stdValid << " valid results, "
       << stdResult.stats.nodesExplored << " nodes" << endl;
  cerr << "SuffixCache: " << cacheValid << " valid results, "
       << cacheResult.stats.nodesExplored << " nodes, "
       << cacheResult.stats.cacheHits << " cache hits" << endl;
  if (cacheValid > stdValid) {
    cerr << "SuffixCache found " << (cacheValid - stdValid) << " MORE results!" << endl;
  }
}

// =============================================================================
// Verify cc + <C-u> for linewise goal with indented lines
// =============================================================================
TEST_F(DebugTest, CcAutoindentCollapse) {
  // Scenario: initial has indented line, goal replaces content.
  // The linewise path uses cc which inherits autoindent from the deleted line.
  // After the fix, <C-u> clears autoindent so collapse <BS> joins lines correctly.
  //
  // Initial: "    indented" (4 spaces indent)
  // Goal:    "replaced"
  // Boundary: no prefix/suffix (full buffer replacement)
  // Expected: cc<C-u>replaced<Esc> (linewise path)
  //
  // Without fix: cc gives autoindent "    ", then <BS> presses remove
  // spaces instead of joining lines → wrong result.

  Lines initial = {"    indented"};
  Lines goal = {"replaced"};

  EditBoundary boundary(initial, Position(0, 0), initial.endPos());

  EditResult result = makeOptimizer().optimizeEdit(
      initial, goal, boundary, params,
      0, 0, Position(0, 0));

  // Verify at least one result is valid
  bool anyValid = false;
  for (size_t i = 0; i < result.resultCount(); i++) {
    if (result.getResults()[i].isValid()) {
      anyValid = true;
      const auto& seq = result.getResults()[i].sequence;
      cerr << "  pos " << i << ": '" << seq << "' cost="
           << result.getResults()[i].keyCost << endl;
    }
  }
  ASSERT_TRUE(anyValid) << "No valid results found";

  // Oracle-verify all results
  auto oracle = make_unique<NeovimOracle>();
  int passed = 0, total = 0;
  for (size_t i = 0; i < result.resultCount(); i++) {
    const Result& r = result.getResults()[i];
    if (!r.isValid()) continue;
    total++;

    Position editPos = fromFlatIndex(static_cast<int>(i), initial);
    auto nvim = oracle->simulate(initial, editPos.line, editPos.col, r.getSequenceString().keys);
    if (nvim.lines == goal) {
      passed++;
    } else {
      cerr << "FAIL pos=" << i << " seq='" << r.sequence
           << "' got=" << nvim.lines << " expected=" << goal << endl;
    }
  }
  EXPECT_EQ(passed, total) << passed << "/" << total << " passed";

  // Multi-line test: two indented lines → single line
  cerr << "\n=== Multi-line indented test ===" << endl;
  Lines initial2 = {"    hello", "        world"};
  Lines goal2 = {"replaced"};
  EditBoundary boundary2(initial2, Position(0, 0), initial2.endPos());

  EditResult result2 = makeOptimizer().optimizeEdit(
      initial2, goal2, boundary2, params,
      0, 0, Position(0, 0));

  int passed2 = 0, total2 = 0;
  for (size_t i = 0; i < result2.resultCount(); i++) {
    const Result& r = result2.getResults()[i];
    if (!r.isValid()) continue;
    total2++;

    Position editPos = fromFlatIndex(static_cast<int>(i), initial2);
    auto nvim = oracle->simulate(initial2, editPos.line, editPos.col, r.getSequenceString().keys);
    if (nvim.lines == goal2) {
      passed2++;
    } else {
      cerr << "FAIL pos=" << i << " seq='" << r.sequence
           << "' got=" << nvim.lines << " expected=" << goal2 << endl;
    }
  }
  EXPECT_EQ(passed2, total2) << "Multi-line: " << passed2 << "/" << total2 << " passed";
}

TEST_F(DebugTest, InvestigateEditOptimizerMultiLineDiff) {
  // Diff 3 from TelescopingChanges:
  //   deleted: "Switzerland\nInconspicuous, even" (2 lines)
  //   inserted: "Florida" (1 line)
  //   prefix: "I saw a pig in barn in " (23 chars)
  //   suffix: "" (end of buffer)
  //   boundary: hasLinesAbove=false (depends on how constructed), hasLinesBelow=false

  Lines deletedLines = {"Switzerland", "Inconspicuous, even"};
  Lines insertedLines = {"Florida"};

  // Reconstruct the buffer context for boundary
  // The intermediate buffer at edit 3 looks like:
  // ["I saw a pig in barn in Switzerland", "Inconspicuous, even"]
  Lines bufferAtEdit3 = {"I saw a pig in barn in Switzerland", "Inconspicuous, even"};
  Position editBeginPos(0, 23);  // 'S' of Switzerland
  Position editEndPos(1, 19);    // one past 'n' of even (half-open, end of buffer content)

  EditBoundary boundary(bufferAtEdit3, editBeginPos, editEndPos);
  cerr << "\n=== EditBoundary ===" << endl;
  cerr << "  prefix: '" << boundary.prefix() << "' (" << boundary.prefix().size() << " chars)" << endl;
  cerr << "  suffix: '" << boundary.suffix() << "' (" << boundary.suffix().size() << " chars)" << endl;
  cerr << "  hasLinesAbove: " << boundary.hasLinesAbove() << endl;
  cerr << "  hasLinesBelow: " << boundary.hasLinesBelow() << endl;

  // Run EditOptimizer with default params
  cerr << "\n=== EditOptimizer (default params) ===" << endl;
  EditOptimizer editOpt(config);
  EditOptimizerParams defaultParams;
  cerr << "  maxNodesExplored=" << defaultParams.maxNodesExplored
       << " maxResults=" << defaultParams.maxResults << endl;

  EditResult result = editOpt.optimizeEdit(
      deletedLines, insertedLines, boundary, defaultParams,
      editBeginPos.line, editBeginPos.col, Position(0, 29));

  cerr << "  stats: nodes=" << result.stats.nodesExplored
       << " results=" << result.stats.resultsFound
       << " queueSize=" << result.stats.queueSizeAtStop
       << " stopReason=" << static_cast<int>(result.stats.stopReason)
       << " skipped=" << result.stats.statesSkipped << endl;

  int validCount = 0;
  for (size_t i = 0; i < result.resultCount(); i++) {
    if (result.getResults()[i].isValid()) {
      validCount++;
      cerr << "  pos " << i << ": '" << result.getResults()[i].sequence
           << "' cost=" << result.getResults()[i].keyCost << endl;
    }
  }
  cerr << "  valid: " << validCount << " / " << result.resultCount() << endl;

  // Run with much higher budget
  cerr << "\n=== EditOptimizer (500k nodes) ===" << endl;
  EditOptimizerParams bigParams = EditOptimizerParams{}
      .withMaxNodesExplored(500000);

  EditResult bigResult = editOpt.optimizeEdit(
      deletedLines, insertedLines, boundary, bigParams,
      editBeginPos.line, editBeginPos.col, Position(0, 29));

  cerr << "  stats: nodes=" << bigResult.stats.nodesExplored
       << " results=" << bigResult.stats.resultsFound
       << " queueSize=" << bigResult.stats.queueSizeAtStop
       << " stopReason=" << static_cast<int>(bigResult.stats.stopReason)
       << " skipped=" << bigResult.stats.statesSkipped << endl;

  int bigValidCount = 0;
  for (size_t i = 0; i < bigResult.resultCount(); i++) {
    if (bigResult.getResults()[i].isValid()) {
      bigValidCount++;
      cerr << "  pos " << i << ": '" << bigResult.getResults()[i].sequence
           << "' cost=" << bigResult.getResults()[i].keyCost << endl;
    }
  }
  cerr << "  valid: " << bigValidCount << " / " << bigResult.resultCount() << endl;

  // Run with Dijkstra mode (no heuristic bias)
  cerr << "\n=== EditOptimizer (Dijkstra) ===" << endl;
  EditOptimizerParams dijkstraParams = EditOptimizerParams::dijkstra(30, 500000);

  EditResult dijResult = editOpt.optimizeEdit(
      deletedLines, insertedLines, boundary, dijkstraParams,
      editBeginPos.line, editBeginPos.col, Position(0, 29));

  cerr << "  stats: nodes=" << dijResult.stats.nodesExplored
       << " results=" << dijResult.stats.resultsFound
       << " queueSize=" << dijResult.stats.queueSizeAtStop
       << " stopReason=" << static_cast<int>(dijResult.stats.stopReason)
       << " skipped=" << dijResult.stats.statesSkipped << endl;

  int dijValidCount = 0;
  for (size_t i = 0; i < dijResult.resultCount(); i++) {
    if (dijResult.getResults()[i].isValid()) {
      dijValidCount++;
      cerr << "  pos " << i << ": '" << dijResult.getResults()[i].sequence
           << "' cost=" << dijResult.getResults()[i].keyCost << endl;
    }
  }
  cerr << "  valid: " << dijValidCount << " / " << dijResult.resultCount() << endl;
}

// ============================================================================
// Lazy mode failure investigation
// ============================================================================

TEST_F(NeovimOracleDebug, InvestigateLazyFailures) {
  // Test full sequences as single oracle calls (mode changes must be in one call)

  // JoinLinesWithResidual: ljDce  bbb ccc<Esc>
  cerr << "=== JoinLinesWithResidual ===" << endl;
  {
    Lines buf = {"aaa", "xxx", "ccc"};
    auto r = oracle_->simulate(buf, 0, 0, "ljDce bbb ccc\x1b");
    cerr << "  Input: " << buf << " pos=(0,0)" << endl;
    cerr << "  Seq: ljDce bbb ccc<Esc>" << endl;
    cerr << "  Got: " << r.lines << " pos=(" << r.row << "," << r.col << ")" << endl;
    cerr << "  Expected: aaa bbb ccc" << endl << endl;
  }

  // Also trace step by step with proper mode handling
  cerr << "=== JoinLinesWithResidual step-by-step ===" << endl;
  {
    auto tracer = makeTracer({"aaa", "xxx", "ccc"}, 0, 0);
    tracer.trace("lj");
    tracer.trace("D");
    tracer.trace("ce bbb ccc\x1b");  // ce + insert text together
    tracer.printSummary();
  }

  // DeleteEntireLine: jd}C b.baaa<Esc>
  cerr << "=== DeleteEntireLine ===" << endl;
  {
    Lines buf = {",ba .e,c", "ede,bb.", "b.baaa"};
    auto r = oracle_->simulate(buf, 0, 0, "jd}C b.baaa\x1b");
    cerr << "  Input: " << buf << " pos=(0,0)" << endl;
    cerr << "  Seq: jd}C b.baaa<Esc>" << endl;
    cerr << "  Got: " << r.lines << " pos=(" << r.row << "," << r.col << ")" << endl;
    cerr << "  Expected: ,ba .e,c | b.baaa" << endl << endl;
  }

  // What does d} actually do from line 1 on 3 non-blank lines?
  cerr << "=== d} from line 1 on 3 non-blank lines ===" << endl;
  {
    auto tracer = makeTracer({",ba .e,c", "ede,bb.", "b.baaa"}, 1, 0);
    tracer.trace("d}");
    tracer.printSummary();
  }

  // TwoEdits_SameLine: rcl rbEwCfed<Esc>
  cerr << "=== TwoEdits_SameLine ===" << endl;
  {
    Lines buf = {"ffb decd bdf"};
    auto r = oracle_->simulate(buf, 0, 0, "rcl rbEwCfed\x1b");
    cerr << "  Input: " << buf << " pos=(0,0)" << endl;
    cerr << "  Seq: rcl rbEwCfed<Esc>" << endl;
    cerr << "  Got: " << r.lines << " pos=(" << r.row << "," << r.col << ")" << endl;
    cerr << "  Expected: cbb decd fed" << endl << endl;
  }

  // Step through the space issue
  cerr << "=== TwoEdits_SameLine step-by-step ===" << endl;
  {
    auto tracer = makeTracer({"ffb decd bdf"}, 0, 0);
    tracer.trace("rc");
    tracer.trace("l");
    tracer.trace("rb");
    tracer.trace("EwCfed\x1b");
    tracer.printSummary();
  }

  // JoinLines: jDcE  bbb ccc?<Esc>
  cerr << "=== JoinLines ===" << endl;
  {
    Lines buf = {"aaa", "bbb", "ccc"};
    auto r = oracle_->simulate(buf, 0, 0, "jDcE bbb ccc?\x1b");
    cerr << "  Input: " << buf << " pos=(0,0)" << endl;
    cerr << "  Seq: jDcE bbb ccc?<Esc>" << endl;
    cerr << "  Got: " << r.lines << " pos=(" << r.row << "," << r.col << ")" << endl;
    cerr << "  Expected: aaa bbb ccc?" << endl << endl;
  }

  // Example1: I Dry-brined <Esc> EEwC excellent<Esc> gegebjcaw agree<Esc>
  cerr << "=== Example1 ===" << endl;
  {
    Lines buf = {"steak is pretty nice", "don't you think?"};
    auto r = oracle_->simulate(buf, 0, 0,
        "I Dry-brined \x1b" "EEwCexcellent\x1b" "gegebjcaw agree\x1b");
    cerr << "  Input: " << buf << " pos=(0,0)" << endl;
    cerr << "  Seq: I Dry-brined <Esc>EEwCexcellent<Esc>gegebjcaw agree<Esc>" << endl;
    cerr << "  Got: " << r.lines << " pos=(" << r.row << "," << r.col << ")" << endl;
    cerr << "  Expected: Dry-brined steak is excellent | don't you agree?" << endl << endl;
  }

  // d} behavior investigation
  cerr << "=== d} on single paragraph (3 lines, no blanks) ===" << endl;
  {
    Lines buf3 = {"line1", "line2", "line3"};
    for (int line = 0; line < 3; line++) {
      auto r = oracle_->simulate(buf3, line, 0, "d}");
      cerr << "  d} from line " << line << ": " << r.lines << endl;
    }
  }
}

// =============================================================================
// ReplayVerification: Edit::applyEdit matches optimizer state transitions
// =============================================================================
// This validates the replay-based suffix cache: when we replay a search
// sequence via Edit::applyEdit, the intermediate (lines, pos) must match
// what the optimizer's EditState transitions produce.

// Helper: apply a single command via Edit::applyEdit
static pair<Lines, Position> applyViaEdit(const Lines& lines, Position pos, string_view cmd) {
  Lines result = lines;
  Mode mode = Mode::Normal;
  auto edits = Edit::parseEdits(cmd);
  for (const auto& e : edits) {
    Edit::applyEdit(result, pos, mode, e);
  }
  return {result, pos};
}

TEST_F(DebugTest, ReplayVerification_Charwise) {
  // Test charwise deletions: Edit::applyEdit vs VimCore::deleteRange
  // (EditState::afterDeletion delegates to VimCore::deleteRange)
  Lines buf = {"hello world", "foo bar"};

  // x from (0,5): delete single char (space)
  {
    Position start(0, 5);
    Range range(start, start);
    auto [editLines, editPos] = applyViaEdit(buf, start, "x");

    Lines coreLines = buf;
    Position corePos = start;
    VimCore::deleteRange(coreLines, range, corePos, Mode::Normal);
    EXPECT_EQ(editLines, coreLines) << "x lines mismatch";
    EXPECT_EQ(editPos.line, corePos.line) << "x line mismatch";
    EXPECT_EQ(editPos.col, corePos.col) << "x col mismatch";
  }

  // D from (0,5): delete to end of line
  {
    Position start(0, 5);
    Range range(start, Position(0, static_cast<int>(buf[0].size()) - 1));
    auto [editLines, editPos] = applyViaEdit(buf, start, "D");

    Lines coreLines = buf;
    Position corePos = start;
    VimCore::deleteRange(coreLines, range, corePos, Mode::Normal);
    EXPECT_EQ(editLines, coreLines) << "D lines mismatch";
    EXPECT_EQ(editPos.line, corePos.line) << "D line mismatch";
    EXPECT_EQ(editPos.col, corePos.col) << "D col mismatch";
  }
}

TEST_F(DebugTest, ReplayVerification_Linewise) {
  // Test dd: Edit::applyEdit vs EditState::afterLinewiseDeletion
  Lines buf = {"first line", "second line", "third line"};

  // dd from line 0
  {
    Position start(0, 3);
    auto [editLines, editPos] = applyViaEdit(buf, start, "dd");

    EditState state(buf, start, 0, 0.0);
    EditState after = state.afterLinewiseDeletion(0);
    EXPECT_EQ(editLines, after.getLines()) << "dd line 0 lines mismatch";
    EXPECT_EQ(editPos.line, after.getPos().line) << "dd line 0 pos.line mismatch";
    EXPECT_EQ(editPos.col, after.getPos().col) << "dd line 0 pos.col mismatch";
    EXPECT_EQ(editPos.targetCol, after.getPos().targetCol) << "dd line 0 targetCol mismatch";
  }

  // dd from line 1
  {
    Position start(1, 5);
    auto [editLines, editPos] = applyViaEdit(buf, start, "dd");

    EditState state(buf, start, 0, 0.0);
    EditState after = state.afterLinewiseDeletion(1);
    EXPECT_EQ(editLines, after.getLines()) << "dd line 1 lines mismatch";
    EXPECT_EQ(editPos.line, after.getPos().line) << "dd line 1 pos.line mismatch";
    EXPECT_EQ(editPos.col, after.getPos().col) << "dd line 1 pos.col mismatch";
    EXPECT_EQ(editPos.targetCol, after.getPos().targetCol) << "dd line 1 targetCol mismatch";
  }

  // dd on last line (buffer becomes single line)
  {
    Position start(2, 0);
    auto [editLines, editPos] = applyViaEdit(buf, start, "dd");

    EditState state(buf, start, 0, 0.0);
    EditState after = state.afterLinewiseDeletion(2);
    EXPECT_EQ(editLines, after.getLines()) << "dd last line lines mismatch";
    EXPECT_EQ(editPos.line, after.getPos().line) << "dd last line pos.line mismatch";
    EXPECT_EQ(editPos.col, after.getPos().col) << "dd last line pos.col mismatch";
    EXPECT_EQ(editPos.targetCol, after.getPos().targetCol) << "dd last line targetCol mismatch";
  }

  // dd with targetCol > line length (tests targetCol reset)
  {
    Lines buf2 = {"long line here", "ab", "medium line"};
    Position start(1, 1, 10);  // col=1 but targetCol=10
    auto [editLines, editPos] = applyViaEdit(buf2, start, "dd");

    EditState state(buf2, start, 0, 0.0);
    EditState after = state.afterLinewiseDeletion(1);
    EXPECT_EQ(editLines, after.getLines()) << "dd targetCol lines mismatch";
    EXPECT_EQ(editPos, after.getPos()) << "dd targetCol pos mismatch";
  }
}

TEST_F(DebugTest, ReplayVerification_Join) {
  // Test J/gJ: Edit::applyEdit vs EditState::afterJoin
  Lines buf = {"hello  ", "  world", "end"};

  // J (add space)
  {
    Position start(0, 2);
    auto [editLines, editPos] = applyViaEdit(buf, start, "J");

    EditState state(buf, start, 0, 0.0);
    EditState after = state.afterJoin(true);
    EXPECT_EQ(editLines, after.getLines()) << "J lines mismatch";
    EXPECT_EQ(editPos, after.getPos()) << "J pos mismatch";
  }

  // gJ (no space)
  {
    Position start(0, 2);
    auto [editLines, editPos] = applyViaEdit(buf, start, "gJ");

    EditState state(buf, start, 0, 0.0);
    EditState after = state.afterJoin(false);
    EXPECT_EQ(editLines, after.getLines()) << "gJ lines mismatch";
    EXPECT_EQ(editPos, after.getPos()) << "gJ pos mismatch";
  }

  // J on empty next line
  {
    Lines buf2 = {"hello", "", "world"};
    Position start(0, 2);
    auto [editLines, editPos] = applyViaEdit(buf2, start, "J");

    EditState state(buf2, start, 0, 0.0);
    EditState after = state.afterJoin(true);
    EXPECT_EQ(editLines, after.getLines()) << "J empty lines mismatch";
    EXPECT_EQ(editPos, after.getPos()) << "J empty pos mismatch";
  }
}

TEST_F(DebugTest, ReplayVerification_Motion) {
  // Test motion commands: Edit::applyEdit position matches setPos
  Lines buf = {"hello world", "foo bar", "end"};

  struct MotionTest { string cmd; Position start; Position expected; };
  vector<MotionTest> tests = {
    {"h", Position(0, 5), Position(0, 4)},
    {"l", Position(0, 5), Position(0, 6)},
    {"j", Position(0, 5), Position(1, 5)},
    {"k", Position(1, 3), Position(0, 3)},
  };

  for (const auto& t : tests) {
    auto [editLines, editPos] = applyViaEdit(buf, t.start, t.cmd);
    EXPECT_EQ(editPos.line, t.expected.line) << t.cmd << " line mismatch";
    EXPECT_EQ(editPos.col, t.expected.col) << t.cmd << " col mismatch";
    EXPECT_EQ(editLines, buf) << t.cmd << " should not modify buffer";
  }
}
