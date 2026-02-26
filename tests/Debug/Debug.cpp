// tests/Debug/Debug.cpp
//
// Debug utilities and scratch tests for development.
// Category: Active (see docs/tests/debug-taxonomy.md).
// Enable a test by removing DISABLED_ prefix.
//
// Run: ./build/tests/vimficiency_debug --gtest_filter="DebugTest.*"
//   - Or: ./vimficiency_tests --gtest_filter="NeovimOracleDebug.*"

#include <gtest/gtest.h>

#include "Interpreter/EditInterpreter.h"
#include "Interpreter/MotionInterpreter.h"
#include "Interpreter/SequenceParser.h"
#include "Keyboard/Config.h"
#include "Optimizer/EditOptimizer/EditOptimizer.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Optimizer/CompositionOptimizer/CompositionSearchContext.h"
#include "Optimizer/CompositionOptimizer/DiffState.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"
#include "Boundary/EditBoundary.h"
#include "Boundary/MotionBoundary.h"
#include "Utils/EditTestGenerators.h"
#include "Utils/NeovimOracle.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/StringUtils.h"
#include "Optimizer/EditOptimizer/EditState.h"
#include "VimCore/VimCore.h"
#include "VimCore/VimEditUtils.h"
#include "VimCore/VimEndpointUtils.h"
#include "VimCore/VimMotionUtils.h"
#include "VimCore/VimPortedImpl.h"

using namespace std;

namespace {
EditResult pureDeletionResult(
    EditOptimizer& opt,
    const Lines& initialLines,
    EditBoundary boundary,
    EditOptimizerParams params = {}) {
  return opt.optimizePureDeletion(initialLines, boundary, params).editResult;
}
} // namespace

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
    return EditBoundary(source, CursorPos(0, 0), source.endPos());
  }
};

TEST_F(DebugTest, DISABLED_InvestigateSuffixCacheCrash) {
  // Reproduce 5L+bnd benchmark crash: "j requires 1 lines below"
  Lines buffer = {"prefix stuff delete me line one",
                  "delete me line two",
                  "delete me line three",
                  "delete me line four",
                  "delete me line five and suffix here"};
  Lines goal = {"replaced"};
  CursorPos editBegin(0, 13);
  CursorPos editEnd(4, 22);

  Lines editRegion = buffer.getSpan(editBegin, editEnd);
  EditBoundary boundary(buffer, editBegin, editEnd);

  // Reconstruct effective lines
  Lines eff = editRegion;
  eff.front().insert(0, boundary.prefix());
  eff.back() += boundary.suffix();

  cerr << "editRegion: " << editRegion << endl;
  cerr << "prefix: '" << boundary.prefix() << "' suffix: '" << boundary.suffix() << "'" << endl;
  cerr << "effectiveLines (" << eff.size() << " lines):" << endl;
  for (size_t i = 0; i < eff.size(); i++) {
    cerr << "  [" << i << "] '" << eff[i] << "'" << endl;
  }

  // Run optimizer and get results, then replay each result to find the crashing one
  EditOptimizer opt(config);
  int mr = max(10, editRegion.totalPositions() / 4);
  EditOptimizerParams p = EditOptimizerParams{}.withMaxResults(mr);

  // The crash is in suffix cache replay (replayAndCacheSuffix).
  // Let's try to find which sequence fails by replaying results manually.
  // First, just replay all possible A* sequences on effectiveLines step-by-step.
  // We'll do this by trying each possible dj/dk-containing sequence.

  // Build a set of test sequences
  vector<string> testSeqs = {
    "djk", "djkdd", "djkddj", "djk.j",
    "ddjdj", "ddjdjk", "djkddk",
    "D4gJ7dbDjd0dw.x",  // Crashing sequence from replayAndCacheSuffix
    "djk.k", "djk..k",
  };

  for (const auto& seq : testSeqs) {
    Lines test = eff;
    CursorPos pos(0, 13);  // first edit position in effective lines
    Mode mode = Mode::Normal;
    string lastEdit;

    auto edits = Edit::parseEdits(seq);
    bool crashed = false;
    cerr << "Replay '" << seq << "': ";
    for (size_t i = 0; i < edits.size(); i++) {
      try {
        Edit::applyEdit(test, pos, mode, edits[i], &lastEdit);
      } catch (const exception& ex) {
        cerr << "CRASH at step " << i << " ('" << edits[i].edit
             << "'): " << ex.what()
             << " | lines=" << test.size()
             << " pos=(" << pos.line << "," << pos.col << ")" << endl;
        crashed = true;
        break;
      }
    }
    if (!crashed) {
      cerr << "OK lines=" << test.size()
           << " pos=(" << pos.line << "," << pos.col << ")" << endl;
    }
  }

  // Detailed step-by-step trace of the crashing sequence
  cerr << "\n--- Step-by-step trace of D4gJ7dbDjd0dw.x ---" << endl;
  {
    Lines test = eff;
    CursorPos pos(0, 13);
    Mode mode = Mode::Normal;
    string lastEdit;
    string seq = "D4gJ7dbDjd0dw.x";
    auto edits = Edit::parseEdits(seq);
    for (size_t i = 0; i < edits.size(); i++) {
      cerr << "  Before step " << i << " ('" << edits[i].edit
           << "' count=" << edits[i].effectiveCount()
           << "): lines=" << test.size()
           << " pos=(" << pos.line << "," << pos.col << ")"
           << " lastEdit='" << lastEdit << "'" << endl;
      for (size_t li = 0; li < test.size(); li++)
        cerr << "    [" << li << "] '" << test[li] << "'" << endl;
      try {
        Edit::applyEdit(test, pos, mode, edits[i], &lastEdit);
      } catch (const exception& ex) {
        cerr << "  CRASH: " << ex.what() << endl;
        break;
      }
    }
  }

  // The crash is inside replayAndCacheSuffix. To find the crashing sequence,
  // we need to instrument the replay. Since we can't add prints to production
  // code, let's directly test: replay each A* goal sequence on effective lines
  // to find which one crashes.
  // First, run a pure-deletion variant (no suffix cache) to check if it crashes.
  cerr << "\n--- Running pure deletion ---" << endl;
  {
    EditOptimizer opt2(config);
    auto result = pureDeletionResult(opt2, editRegion, boundary, p);
    cerr << "Pure deletion OK, results=" << result.resultCount() << endl;

    // Now replay each result
    for (size_t i = 0; i < result.resultCount(); i++) {
      const auto& r = result.getResults()[i];
      if (!r.isValid()) continue;
      const string& seq = r.getSequenceString().str();

      Lines test = eff;
      CursorPos pos = fromFlatIndex(static_cast<int>(i), editRegion);
      pos.col += (pos.line == 0 ? boundary.prefix().size() : 0);
      Mode mode = Mode::Normal;
      string lastEdit;

      auto edits = Edit::parseEdits(seq);
      for (size_t j = 0; j < edits.size(); j++) {
        try {
          Edit::applyEdit(test, pos, mode, edits[j], &lastEdit);
        } catch (const exception& ex) {
          cerr << "REPLAY CRASH idx=" << i << " seq='" << seq << "' step=" << j
               << " edit='" << edits[j].edit << "' count=" << edits[j].effectiveCount()
               << ": " << ex.what() << endl;
          cerr << "  lines=" << test << " pos=(" << pos.line << "," << pos.col << ")" << endl;
          break;
        }
      }
    }
  }

  cerr << "\n--- Running full optimizer (edit with goal) ---" << endl;
  try {
    auto result = opt.optimizeEdit(editRegion, goal, boundary, p);
    cerr << "Full optimizer OK, results=" << result.resultCount() << endl;
  } catch (const exception& ex) {
    cerr << "OPTIMIZER CRASH: " << ex.what() << endl;
  }

  // Test without counted edits: use minCountRepeat=999 to disable
  cerr << "\n--- Without counted edits (minCountRepeat=999) ---" << endl;
  try {
    EditOptimizer opt3(config);
    EditOptimizerParams p3 = EditOptimizerParams{}.withMaxResults(mr).withMinCountRepeat(999);
    auto result = opt3.optimizeEdit(editRegion, goal, boundary, p3);
    cerr << "No-counted OK, results=" << result.resultCount() << endl;
  } catch (const exception& ex) {
    cerr << "No-counted CRASH: " << ex.what() << endl;
  }
}

TEST_F(DebugTest, DISABLED_InvestigateCountedWordEdit) {
  auto oracle = make_unique<NeovimOracle>();

  // Reproduce MultiLineEmbedded iter=0 failure
  // FullBuffer: acffce\nadf\n ,e\n.fe bd
  // EditRegion: cffce\nadf\n ,e\n.fe   (prefix="a", suffix="bd")
  // Failing seq: 'de4Xx' from editPos=[0,4] bufferPos=[0,5]
  // Expected: 'abd', Got: '\n ,e\n.fe bd'
  cerr << "=== Investigate CountedWordEdit Bug ===" << endl;

  Lines fullBuffer = {"acffce", "adf", " ,e", ".fe bd"};
  Lines editRegion = {"cffce", "adf", " ,e", ".fe "};
  string expected = "abd";
  int bufRow = 0, bufCol = 5;

  // Step 1: Trace the sequence step-by-step in Neovim
  cerr << "\n--- Step-by-step trace in Neovim ---" << endl;
  {
    SequenceTracer tracer(oracle.get(), fullBuffer, bufRow, bufCol);
    tracer.trace("de");
    tracer.trace("X");
    tracer.trace("X");
    tracer.trace("X");
    tracer.trace("X");
    tracer.trace("x");
    tracer.printSummary();
  }

  // Step 2: Trace collapsed version in Neovim
  cerr << "\n--- Collapsed trace in Neovim ---" << endl;
  {
    SequenceTracer tracer(oracle.get(), fullBuffer, bufRow, bufCol);
    tracer.trace("de");
    tracer.trace("4X");
    tracer.trace("x");
    tracer.printSummary();
  }

  // Step 3: Full sequence at once
  cerr << "\n--- Full sequences ---" << endl;
  {
    auto r1 = oracle->simulate(fullBuffer, bufRow, bufCol, "deXXXXx");
    cerr << "deXXXXx: " << r1.lines << " flat='" << r1.lines.flatten() << "'" << endl;
    auto r2 = oracle->simulate(fullBuffer, bufRow, bufCol, "de4Xx");
    cerr << "de4Xx:   " << r2.lines << " flat='" << r2.lines.flatten() << "'" << endl;
  }

  // Step 4: Our simulator step-by-step
  cerr << "\n--- Our simulator step-by-step ---" << endl;
  {
    Lines simBuf = fullBuffer;
    CursorPos simPos(bufRow, bufCol);
    Mode mode = Mode::Normal;
    string lastEdit;

    auto edits = Edit::parseEdits("deXXXXx");
    for (const auto& e : edits) {
      cerr << "Before '" << e.edit << "': buf=" << simBuf << " pos=(" << simPos.line << "," << simPos.col << ")" << endl;
      Edit::applyEdit(simBuf, simPos, mode, e, &lastEdit);
      cerr << "After:  buf=" << simBuf << " pos=(" << simPos.line << "," << simPos.col << ")" << endl;
    }
    cerr << "Final flat: '" << simBuf.flatten() << "'" << endl;
  }

  // Step 5: Trace on EFFECTIVE lines (with prefix/suffix baked in)
  cerr << "\n--- Our simulator on effective lines ---" << endl;
  {
    Lines effLines = {"acffce", "adf", " ,e", ".fe bd"};
    CursorPos simPos(0, 5);  // editPos [0,4] + leftColOffset 1 = col 5
    Mode mode = Mode::Normal;
    string lastEdit;

    auto edits = Edit::parseEdits("deX...x");
    for (const auto& e : edits) {
      cerr << "Before '" << e.edit << "': buf=" << effLines << " pos=(" << simPos.line << "," << simPos.col << ")" << endl;
      Edit::applyEdit(effLines, simPos, mode, e, &lastEdit);
      cerr << "After:  buf=" << effLines << " pos=(" << simPos.line << "," << simPos.col << ")" << endl;
    }
    cerr << "Final: " << effLines << " flat='" << effLines.flatten() << "'" << endl;
    cerr << "Goal check: size=" << effLines.size() << " [0]='" << effLines[0] << "'" << endl;
  }

  // Step 5b: Check hash collision potential - compute hash at different intermediate states
  cerr << "\n--- Hash collision check ---" << endl;
  {
    Lines goal = {"abd"};
    size_t goalHash = hashLines(goal);
    cerr << "Goal hash: " << goalHash << " lines=" << goal << endl;

    // After deXXXXx on effective lines
    Lines afterSeq = {"", " ,e", ".fe bd"};
    size_t seqHash = hashLines(afterSeq);
    cerr << "After deXXXXx hash: " << seqHash << " lines=" << afterSeq << endl;
    cerr << "Hash collision: " << (goalHash == seqHash ? "YES" : "no") << endl;
  }

  // Step 6: Now run optimizer and see what it produces
  cerr << "\n--- Optimizer output ---" << endl;
  {
    EditOptimizer opt(config);
    EditBoundary boundary(fullBuffer, CursorPos(0, 1), CursorPos(3, 3));  // prefix="a", suffix="bd"
    EditResult res = pureDeletionResult(opt, editRegion, boundary, params);
    int idx = 0;
    for (int line = 0; line < static_cast<int>(editRegion.size()); line++) {
      int cols = editRegion[line].empty() ? 1 : static_cast<int>(editRegion[line].size());
      for (int col = 0; col < cols; col++) {
        const Result& r = res.getResults()[idx];
        if (r.isValid() && idx == 4) {  // editPos [0,4]
          cerr << "editPos=[" << line << "," << col << "] seq='" << r.getSequenceString() << "' cost=" << r.keyCost << endl;
        }
        idx++;
      }
    }
  }
}

TEST_F(DebugTest, DISABLED_DiffRegionInvestigation) {
  // Investigate how Myers diff splits buffer changes into regions,
  // and whether pre-A* line trimming would add value beyond what
  // CompositionOptimizer already handles via Myers.

  auto printDiffs = [](const Lines& initial, const Lines& goal, const string& label) {
    auto diffs = Myers::calculate(initial, goal);
    cerr << "=== " << label << " ===" << endl;
    cerr << "  initial: " << initial << endl;
    cerr << "  goal:    " << goal << endl;
    cerr << "  " << diffs.size() << " diff region(s):" << endl;
    for (size_t i = 0; i < diffs.size(); i++) {
      const auto& d = diffs[i];
      const char* kind = d.isPureInsertion() ? "INSERT"
                       : d.isPureDeletion()  ? "DELETE"
                                             : "REPLACE";
      cerr << "    [" << i << "] " << kind
           << " (" << d.beginPos.line << "," << d.beginPos.col << ")"
           << "->(" << d.endPos.line << "," << d.endPos.col << ")"
           << " del=\"" << d.deletedText << "\" ins=\"" << d.insertedText << "\""
           << " pre=\"" << d.boundary.prefix() << "\" suf=\"" << d.boundary.suffix() << "\""
           << " above=" << d.boundary.hasLinesAbove()
           << " below=" << d.boundary.hasLinesBelow() << endl;
    }
    cerr << endl;
  };

  // Case 1: Matching prefix+suffix lines — Myers should isolate the middle change
  printDiffs({"aaa", "bbb", "ccc"}, {"aaa", "xxx", "ccc"},
             "Matching prefix+suffix lines");

  // Case 2: Matching prefix lines only
  printDiffs({"aaa", "bbb", "ccc"}, {"aaa", "xxx", "ddd"},
             "Matching prefix lines only");

  // Case 3: Matching suffix lines only
  printDiffs({"aaa", "bbb", "ccc"}, {"xxx", "yyy", "ccc"},
             "Matching suffix lines only");

  // Case 4: Sub-line prefix match (interesting for EditOptimizer trimming)
  printDiffs({"hello world"}, {"hello earth"},
             "Sub-line prefix match");

  // Case 5: Sub-line suffix match
  printDiffs({"hello world"}, {"goodbye world"},
             "Sub-line suffix match");

  // Case 6: Sub-line both ends match
  printDiffs({"the quick brown fox"}, {"the slow brown fox"},
             "Sub-line both ends match");

  // Case 7: Multi-line with common indent
  printDiffs({"    if (x) {", "        foo();", "    }"},
             {"    if (x) {", "        bar();", "    }"},
             "Common indent multi-line");

  // Case 8: Multiple scattered changes on one line
  printDiffs({"abcdefghij"}, {"xbcdeyghij"},
             "Multiple scattered single-char changes");

  // Case 9: Line insertion (pure insertion)
  printDiffs({"aaa", "ccc"}, {"aaa", "bbb", "ccc"},
             "Line insertion");

  // Case 10: Line deletion (pure deletion)
  printDiffs({"aaa", "bbb", "ccc"}, {"aaa", "ccc"},
             "Line deletion");

  // Case 11: Realistic code edit — variable rename
  printDiffs({"int count = 0;", "count++;", "return count;"},
             {"int total = 0;", "total++;", "return total;"},
             "Variable rename (count->total)");

  // Case 12: Large matching context, small change
  printDiffs({"line1", "line2", "line3", "line4", "CHANGE", "line6", "line7", "line8"},
             {"line1", "line2", "line3", "line4", "FIXED",  "line6", "line7", "line8"},
             "Large context, single line change");

  EXPECT_TRUE(true);
}

TEST_F(DebugTest, DISABLED_Placeholder) {
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
    CursorPos initialPos(0, 0);

    auto compResult = opt.optimize(initial, initialPos, goal, CursorPos(0, 0), params);
    const auto& results = compResult.results;

    cerr << "Results: " << results.size() << endl;
    for (size_t i = 0; i < results.size(); i++) {
      const auto& seq = results[i].sequence;
      // Print sequence bytes
      cerr << "  [" << i << "] seq='" << seq << "' (len=" << seq.size() << ")" << endl;
      cerr << "       bytes: ";
      for (char c : seq.view()) cerr << static_cast<int>(static_cast<unsigned char>(c)) << " ";
      cerr << endl;
      cerr << "       cost=" << results[i].keyCost << endl;

      auto nvim = oracle->simulate(initial, 0, 0, seq.str());
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

    auto compResult = opt.optimize(initial, CursorPos(0,0), goal, CursorPos(0,0), params);
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

TEST_F(NeovimOracleDebug, DISABLED_InvestigateCwWhitespaceEOF) {
  auto probe = [&](Lines source, int row, int col, string_view seq) {
    auto nvim = oracle_->simulate(source, row, col, string(seq));

    Lines interp = source;
    CursorPos pos(row, col);
    Mode mode = Mode::Normal;
    string lastEdit;
    for (const auto& e : Edit::parseEdits(seq)) {
      Edit::applyEdit(interp, pos, mode, e, &lastEdit);
    }

    cerr << "seq='" << seq << "' src=" << source
         << " start=(" << row << "," << col << ")\n";
    cerr << "  nvim: " << nvim.lines << " pos=(" << nvim.row << "," << nvim.col
         << ") mode=" << (nvim.mode == Mode::Normal ? "N" : "I") << "\n";
    cerr << "  ours: " << interp << " pos=(" << pos.line << "," << pos.col
         << ") mode=" << (mode == Mode::Normal ? "N" : "I") << "\n";
    int nvimLen = nvim.lines.empty() ? -1 : static_cast<int>(nvim.lines[0].size());
    int oursLen = interp.empty() ? -1 : static_cast<int>(interp[0].size());
    bool same = (nvim.lines == interp && nvim.row == pos.line &&
                 nvim.col == pos.col && nvim.mode == mode);
    cerr << "  lens: nvim=" << nvimLen << " ours=" << oursLen
         << " match=" << (same ? "yes" : "no") << "\n";
  };

  probe({" "}, 0, 0, "cwX<Esc>");
  probe({" "}, 0, 0, "cw");
  probe({" "}, 0, 0, "ceX<Esc>");
  probe({" "}, 0, 0, "dw");
  probe({"  "}, 0, 0, "cwX<Esc>");
  probe({"  "}, 0, 0, "ceX<Esc>");
  probe({"  "}, 0, 0, "dw");
  probe({"a "}, 0, 1, "cwX<Esc>");
  probe({"a "}, 0, 1, "ceX<Esc>");
  probe({"a "}, 0, 0, "dw");
  probe({"a "}, 0, 0, "de");
  probe({"a "}, 0, 0, "ceX<Esc>");
  probe({"a "}, 0, 0, "cwX<Esc>");
  probe({"a "}, 0, 0, "cw");
}

TEST_F(NeovimOracleDebug, DISABLED_InvestigateDotDbBug) {
  // FAIL iter=11 seq='db..s fba<Esc>' initialPos=(0,2)
  //   Initial: 'bbcffdbeafcbfabbdc'
  //   Edit:     [0,5) 'bbcff' -> 'fba'
  //   Goal:    'fbadbeafcbfabbdc'
  //   Got:     fbaffdbeafcbfabbdc

  Lines initial = {"bbcffdbeafcbfabbdc"};
  Lines goal = {"fbadbeafcbfabbdc"};
  CursorPos initialPos(0, 2);

  // Step 1: Trace the winning sequence step by step with oracle
  cerr << "=== Oracle trace of db..s fba<Esc> ===" << endl;
  {
    auto tracer = makeTracer(initial, 0, 2);
    tracer.trace("db");
    tracer.trace(".");   // repeats db
    tracer.trace(".");   // repeats db again
    tracer.traceFullSequence("db..s fba\x1b");
  }

  // Step 2: Run the composition optimizer and show what it finds
  cerr << "\n=== Composition Optimizer ===" << endl;
  {
    Config config = Config::uniform();
    CompositionOptimizer opt{config};
    CompositionOptimizerParams params = CompositionOptimizerParams{}.withMaxResults(5);
    auto compResult = opt.optimize(initial, initialPos, goal, CursorPos(0, 0), params);
    for (size_t i = 0; i < compResult.results.size(); i++) {
      const auto& seq = compResult.results[i].getSequenceString();
      cerr << "  [" << i << "] '" << compResult.results[i].sequence
           << "' cost=" << compResult.results[i].keyCost << endl;
      auto nvim = oracle_->simulate(initial, 0, 2, seq.str());
      cerr << "    nvim: " << nvim.lines << (nvim.lines == goal ? " OK" : " WRONG") << endl;
    }
  }

  // Step 3: Run the edit optimizer directly on the diff
  cerr << "\n=== Edit Optimizer for [0,5) ===" << endl;
  {
    Config config = Config::uniform();
    auto diffs = Myers::calculate(initial, goal);
    for (size_t i = 0; i < diffs.size(); i++) {
      const auto& d = diffs[i];
      cerr << "  Diff[" << i << "] begin=(" << d.beginPos.line << "," << d.beginPos.col
           << ") end=(" << d.endPos.line << "," << d.endPos.col << ")"
           << " del='" << d.deletedText << "' ins='" << d.insertedText << "'"
           << " prefix='" << d.boundary.prefix() << "' suffix='" << d.boundary.suffix() << "'"
           << endl;

      if (!d.isPureInsertion()) {
        EditOptimizer editOpt(config);
        EditOptimizerParams params = EditOptimizerParams{}.withMaxResults(INT_MAX);
        EditResult result = editOpt.optimizeEdit(
            d.deletedLines(), d.insertedLines(), d.boundary, params,
            d.beginPos.line, d.beginPos.col, d.beginPos);
        for (size_t j = 0; j < result.resultCount(); j++) {
          const auto& r = result.getResults()[j];
          if (r.isValid()) {
            cerr << "    pos " << j << ": '" << r.sequence
                 << "' cost=" << r.keyCost << endl;
            // Verify each with oracle
            int fullCol = static_cast<int>(j) + (d.beginPos.line == 0 ? d.beginPos.col : 0);
            auto nvim = oracle_->simulate(initial, 0, fullCol, r.sequence.str());
            if (nvim.lines != goal) {
              cerr << "      MISMATCH: got " << nvim.lines << endl;
            }
          }
        }
      }
    }
  }

  // Step 4: Trace what our simulator thinks db does vs Neovim
  cerr << "\n=== Our sim vs Oracle for db at various positions ===" << endl;
  {
    Lines buf = {"bbcffdbeafcbfabbdc"};
    for (int col : {0, 1, 2, 3}) {
      // Our sim
      CursorPos pos(0, col);
      Lines simBuf = buf;
      CursorPos simEndpoint = VimCore::motionWordEndpoint<false, EdgeType::WordEdge>(
          pos, simBuf, false, true, 0, false, false);
      cerr << "  col=" << col << " b_endpoint=(" << simEndpoint.line << "," << simEndpoint.col << ")";
      if (simEndpoint == pos) {
        cerr << " (no move)" << endl;
      } else {
        // Compute range like the explorer does (isExclusiveAtCursor)
        Range range;
        if (col > 0) {
          range = Range(simEndpoint, CursorPos(0, col - 1));
        } else {
          cerr << " (col=0, exclusive skip)" << endl;
          continue;
        }
        Lines afterBuf = buf;
        CursorPos afterPos = pos;
        VimCore::deleteRange(afterBuf, range, afterPos);
        cerr << " range=(" << range.begin.col << "," << range.end.col << ")"
             << " after='" << afterBuf[0] << "' pos=" << afterPos.col << endl;
      }

      // Oracle
      auto nvim = oracle_->simulate(buf, 0, col, "db");
      cerr << "    oracle: '" << nvim.lines[0] << "' pos=" << nvim.col << endl;
    }
  }
}

TEST_F(NeovimOracleDebug, DISABLED_InvestigateJoinLinesResidual) {
  // CompositionOptimizer_ManualTest.JoinLinesWithResidual
  // Initial: aaa\nxxx\nccc → Goal: aaa bbb ccc
  // Diff {0}='\nxxx\nccc' → ' bbb ccc'
  // Edit region: lines after "aaa" (prefix="aaa", suffix="")
  cerr << "=== Edit optimizer for JoinLinesWithResidual ===" << endl;

  Lines fullBuffer = {"aaa", "xxx", "ccc"};
  // Edit region: the content to change is "\nxxx\nccc" starting at (0,3)
  // initialLines = ["", "xxx", "ccc"] (3 lines - the newline creates empty first line)
  Lines editRegion = {"", "xxx", "ccc"};
  Lines goalLines = {" bbb ccc"};

  CursorPos initialPos(0, 3);  // start of edit region in full buffer
  CursorPos endPos = fullBuffer.endPos();  // end of buffer
  EditBoundary boundary(fullBuffer, initialPos, endPos);

  cerr << "  prefix='" << boundary.prefix() << "' suffix='" << boundary.suffix() << "'" << endl;
  cerr << "  editRegion=" << editRegion << " goalLines=" << goalLines << endl;

  Config config = Config::uniform();
  EditOptimizer opt(config);
  EditOptimizerParams params = EditOptimizerParams{}.withMaxResults(INT_MAX);
  EditResult res = opt.optimizeEdit(editRegion, goalLines, boundary, params);

  int idx = 0;
  for (int r = 0; r < static_cast<int>(editRegion.size()); r++) {
    for (int c = 0; c < editRegion[r].effectiveSize(); c++) {
      const Result& result = res.getResults()[idx];
      if (result.isValid()) {
        cerr << "  [" << r << "," << c << "] seq='" << result.sequence
             << "' cost=" << result.keyCost << endl;

        // Byte dump for debugging
        cerr << "    bytes:";
        for (unsigned char ch : result.sequence.view()) {
          if (ch >= 0x20 && ch < 0x7f) cerr << " '" << ch << "'";
          else cerr << " 0x" << std::hex << (int)ch << std::dec;
        }
        cerr << endl;

        // Verify with oracle: apply in the full buffer context
        int fullRow = r + static_cast<int>(initialPos.line);
        int fullCol = c + (r == 0 ? static_cast<int>(initialPos.col) : 0);
        auto nvim = oracle_->simulate(fullBuffer, fullRow, fullCol, result.sequence.str());
        Lines goal = {"aaa bbb ccc"};
        if (nvim.lines != goal) {
          cerr << "    MISMATCH: got " << nvim.lines << " expected " << goal << endl;
        } else {
          cerr << "    OK" << endl;
        }
      }
      idx++;
    }
  }
}

TEST_F(NeovimOracleDebug, DISABLED_InvestigateJoinBehavior) {
  // FAIL iter=20 seq='jDJ'
  //   Initial: debceb,\nd a, a\n .ec
  //   Goal: debceb,\n .ec
  //   Got: debceb,\n.ec
  cerr << "=== jDJ step-by-step ===" << endl;
  {
    auto tracer = makeTracer({"debceb,", "d a, a", " .ec"}, 0, 0);
    tracer.trace("j");
    tracer.trace("D");
    tracer.trace("J");
    tracer.printSummary();
  }

  // Also test J behavior on empty line joining with leading-space line
  cerr << endl << "=== J on empty line + leading space ===" << endl;
  {
    auto tracer = makeTracer({"debceb,", "", " .ec"}, 1, 0);
    tracer.trace("J");
    tracer.printSummary();
  }

  // Compare our sim
  cerr << endl << "=== Our sim for J on empty + leading space ===" << endl;
  {
    Lines simLines = {"debceb,", "", " .ec"};
    CursorPos simPos(1, 0);
    Mode simMode = Mode::Normal;
    Edit::applyEdit(simLines, simPos, simMode, Edit::parseEdits("J")[0]);
    cerr << "  Lines: " << simLines << " pos=" << simPos << endl;
  }

  // Also check: J from various line contents
  auto testJ = [&](Lines source, int row, int col) {
    cerr << "=== J on " << source << " from (" << row << "," << col << ") ===" << endl;
    auto nvim = oracle_->simulate(source, row, col, "J");
    cerr << "  Neovim: " << nvim.lines << " cursor=(" << nvim.row << "," << nvim.col << ")" << endl;

    Lines simLines = source;
    CursorPos simPos(row, col);
    Mode simMode = Mode::Normal;
    Edit::applyEdit(simLines, simPos, simMode, Edit::parseEdits("J")[0]);
    cerr << "  Ours:   " << simLines << " cursor=" << simPos << endl;

    bool match = (simLines == nvim.lines && simPos.line == nvim.row && simPos.col == nvim.col);
    cerr << "  " << (match ? "MATCH" : "MISMATCH") << endl << endl;
  };

  testJ({"abc", "def"}, 0, 0);          // normal join
  testJ({"abc", " def"}, 0, 0);         // next line has leading space
  testJ({"abc", "  def"}, 0, 0);        // next line has leading spaces
  testJ({"", "def"}, 0, 0);             // empty current line
  testJ({"", " def"}, 0, 0);            // empty current + leading space
  testJ({"abc ", "def"}, 0, 0);         // current ends with space
  testJ({"abc.", "def"}, 0, 0);         // current ends with period
  testJ({"abc", ""}, 0, 0);             // next line is empty
  testJ({"", ""}, 0, 0);                // both empty
}

TEST_F(NeovimOracleDebug, DISABLED_InvestigateCCloseBrace) {
  // c} vs d}: Vim's exclusive-linewise rule. When exclusive motion lands at
  // col 0: d converts to linewise, c stays characterwise (backs up one char).
  // Use c}X<Esc> to compare in normal mode (avoids <Esc> cursor-backup noise).

  auto testBoth = [&](Lines source, int row, int col) {
    cerr << "=== source=" << source << " pos=(" << row << "," << col << ") ===" << endl;

    // d}
    auto dResult = oracle_->simulate(source, row, col, "d}");
    cerr << "  d}: " << dResult.lines << " cursor=(" << dResult.row << "," << dResult.col << ")" << endl;

    // c}X<Esc> — type "X" so we can see where insert mode cursor was
    auto cResult = oracle_->simulate(source, row, col, "c}X<Esc>");
    cerr << "  c}X<Esc>: " << cResult.lines << " cursor=(" << cResult.row << "," << cResult.col << ")" << endl;

    // Our sim for d}
    {
      Lines simLines = source;
      CursorPos simPos(row, col);
      Mode simMode = Mode::Normal;
      auto edits = Edit::parseEdits("d}");
      for (const auto& e : edits) Edit::applyEdit(simLines, simPos, simMode, e);
      bool match = (simLines == dResult.lines && simPos.line == dResult.row && simPos.col == dResult.col);
      cerr << "  d} sim: " << simLines << " cursor=" << simPos << (match ? " MATCH" : " MISMATCH") << endl;
    }

    // Our sim for c}X<Esc>
    {
      Lines simLines = source;
      CursorPos simPos(row, col);
      Mode simMode = Mode::Normal;
      auto edits = Edit::parseEdits("c}");
      for (const auto& e : edits) Edit::applyEdit(simLines, simPos, simMode, e);
      // Type "X" in insert mode
      VimCore::insertText(simLines, simPos, "X");
      // <Esc> backs up one
      if (simPos.col > 0) simPos.setCol(simPos.col - 1);
      bool match = (simLines == cResult.lines && simPos.line == cResult.row && simPos.col == cResult.col);
      cerr << "  c}X sim: " << simLines << " cursor=" << simPos << (match ? " MATCH" : " MISMATCH") << endl;
    }
    cerr << endl;
  };

  // Key case: cursor at col 0, } lands at col 0 on next blank line
  testBoth({"abc", "def"}, 0, 0);       // single paragraph, 2 lines, EOF
  testBoth({"abc", "def", "ghi"}, 0, 0); // single paragraph, 3 lines, EOF
  testBoth({"abc", "def", "ghi"}, 1, 0); // from middle line, EOF
  testBoth({"abc", "", "def"}, 0, 0);    // two paragraphs, lands at col 0
  testBoth({"abc", "", "def"}, 0, 1);    // two paragraphs, not at col 0
  testBoth({"abc", "def"}, 0, 1);        // single paragraph, EOF, pos.col != 0

  // The failing test case: aaa/xxx/ccc, cursor at (1,0)
  testBoth({"aaa", "xxx", "ccc"}, 1, 0);

  // Does pos.col matter for the linewise conversion?
  testBoth({"abc", "def", "", "ghi"}, 0, 0);
  testBoth({"abc", "def", "", "ghi"}, 0, 1);
  testBoth({"abc", "def", "", "ghi"}, 0, 2);

  // Compare c}<BS> vs d}i vs d}A for the failing case
  cerr << "=== Comparing alternatives for aaa/xxx/ccc from (1,0) ===" << endl;
  {
    Lines src = {"aaa", "xxx", "ccc"};
    auto r1 = oracle_->simulate(src, 1, 0, "c}\x08 bbb ccc<Esc>");
    cerr << "  c}<BS> bbb ccc<Esc>: " << r1.lines << " cursor=(" << r1.row << "," << r1.col << ")" << endl;

    auto r2 = oracle_->simulate(src, 1, 0, "d}i bbb ccc<Esc>");
    cerr << "  d}i bbb ccc<Esc>: " << r2.lines << " cursor=(" << r2.row << "," << r2.col << ")" << endl;

    auto r3 = oracle_->simulate(src, 1, 0, "d}A bbb ccc<Esc>");
    cerr << "  d}A bbb ccc<Esc>: " << r3.lines << " cursor=(" << r3.row << "," << r3.col << ")" << endl;
  }

  // Also test the atCol0 case: abc/def//ghi from (0,0)
  cerr << "=== Comparing alternatives for abc/def//ghi from (0,0) ===" << endl;
  {
    Lines src = {"abc", "def", "", "ghi"};
    auto r1 = oracle_->simulate(src, 0, 0, "c}\x08X<Esc>");
    cerr << "  c}<BS>X<Esc>: " << r1.lines << " cursor=(" << r1.row << "," << r1.col << ")" << endl;

    auto r2 = oracle_->simulate(src, 0, 0, "d}iX<Esc>");
    cerr << "  d}iX<Esc>: " << r2.lines << " cursor=(" << r2.row << "," << r2.col << ")" << endl;
  }
}

TEST_F(NeovimOracleDebug, DISABLED_InvestigateDCloseBrace) {
  // d} is characterwise - deletes from cursor pos to end of paragraph
  // Verify our sim matches neovim

  auto testCase = [&](Lines source, int row, int col) {
    cerr << "=== d} on " << source << " from (" << row << "," << col << ") ===" << endl;

    // Oracle
    auto nvim = oracle_->simulate(source, row, col, "d}");
    cerr << "  Neovim: " << nvim.lines << " cursor=(" << nvim.row << "," << nvim.col << ")" << endl;

    // Our sim
    Lines simLines = source;
    CursorPos simPos(row, col);
    Mode simMode = Mode::Normal;
    auto edits = Edit::parseEdits("d}");
    for (const auto& e : edits) Edit::applyEdit(simLines, simPos, simMode, e);
    cerr << "  Ours:   " << simLines << " cursor=" << simPos << endl;

    bool match = (simLines == nvim.lines && simPos.line == nvim.row && simPos.col == nvim.col);
    cerr << "  " << (match ? "MATCH" : "MISMATCH") << endl << endl;
  };

  // Single line cases
  testCase({" eceba"}, 0, 0);
  testCase({" eceba"}, 0, 2);
  testCase({" eceba"}, 0, 5);
  testCase({"hello"}, 0, 0);
  testCase({"hello"}, 0, 2);

  // Multi-line (single paragraph)
  testCase({"abc", "def"}, 0, 0);
  testCase({"abc", "def"}, 0, 1);
  testCase({"abc", "def"}, 1, 0);
  testCase({"abc", "def"}, 1, 1);

  // Multi-line with blank line (two paragraphs)
  testCase({"abc", "", "def"}, 0, 0);
  testCase({"abc", "", "def"}, 0, 1);
  testCase({"abc", "", "def"}, 2, 0);
}

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
    CursorPos initialPos(0, 0);

    Config config = Config::uniform();
    CompositionOptimizer opt{config};
    CompositionOptimizerParams params = CompositionOptimizerParams{}.withMaxResults(10);

    auto compResult = opt.optimize(
        initial, initialPos, goal, CursorPos(0,0), params);
    const auto& results = compResult.results;

    cerr << "Results: " << results.size() << endl;
    for (size_t i = 0; i < results.size(); i++) {
      const auto& seq = results[i].sequence;
      cerr << "  " << i << ": '" << seq << "' cost=" << results[i].keyCost << endl;

      auto nvim = oracle_->simulate(initial, 0, 0, seq.str());
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
        initial, CursorPos(0, 0), goal, "",
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
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);

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
    CursorPos rangeBegin(0, 6);
    CursorPos rangeEnd(0, 11);

    cerr << "Finding path from " << initialPos << " to range [" << rangeBegin << ", " << rangeEnd << ")" << endl;

    auto rangeResult = movOpt.optimizeToRange(
        initial, initialPos, rangeBegin, rangeEnd,
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
    auto nvim = oracle_->simulate(initial, initialPos.line, initialPos.col, seq.str());
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
  CursorPos initialPos(0, 0);

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
    CursorPos pos(0, 0);

    // Simulate j: move down, preserve targetCol
    pos.line = 1;
    int lastCol = ourLines[1].empty() ? 0 : static_cast<int>(ourLines[1].size()) - 1;
    pos.clampColPreservingTarget(std::min(pos.targetCol, lastCol));
    cerr << "  After j: pos=(" << pos.line << "," << pos.col
         << ") targetCol=" << pos.targetCol << endl;
    cerr << "  Char at cursor: '" << ourLines[pos.line][pos.col] << "'" << endl;

    // Simulate ciw: compute text object range
    Range iwRange = VimCore::textObject(pos, ourLines, /*isInner=*/true, /*isBigWord=*/false);
    cerr << "  textObject(iw) range: [(" << iwRange.begin.line << "," << iwRange.begin.col
         << "), (" << iwRange.end.line << "," << iwRange.end.col << ")]" << endl;
    cerr << "  Deleted text: '";
    for (int c = iwRange.begin.col; c <= iwRange.end.col; c++) {
      cerr << ourLines[iwRange.begin.line][c];
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
  CursorPos initialPos(0, 0);
  CursorPos goalPos(0, 0);
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
      auto nvim = oracle->simulate(initial, initialPos.line, initialPos.col, actualResults[i].sequence.str());
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
    CursorPos rangeBegin = d.beginPos;
    CursorPos rangeEnd = d.endPos;

    cerr << "  Range: [(" << rangeBegin.line << "," << rangeBegin.col << "), ("
         << rangeEnd.line << "," << rangeEnd.col << "))" << endl;
    cerr << "  StartPos: (" << initialPos.line << "," << initialPos.col << ")" << endl;

    MotionBoundary boundary(initial,
        CursorPos(0, 0),
        CursorPos(0, static_cast<int>(initial[0].size())),
        false, false);

    MotionOptimizer motionOpt(config);
    NavContext navCtx;

    auto rangeResult = motionOpt.optimizeToRange(
        initial, initialPos, rangeBegin, rangeEnd,
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
        CursorPos(0, 0),
        CursorPos(0, static_cast<int>(initial[0].size()) - 1),
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
      CursorPos pos = s.getPos();
      int editsCompleted = s.getEditsCompleted();
      popCount++;

      if (ctx.isGoal(s)) {
        cerr << "  POP " << popCount << ": GOAL pos=(" << pos.line << "," << pos.col
             << ") edits=" << editsCompleted
             << " seq='" << s.getSequence() << "' effort=" << s.getEffort()
             << " cost=" << s.getCost() << endl;
        results.emplace_back(s.getSequence().str(), s.getRunningEffort().getEffort(config));
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
        int editEndLine = nextEdit.endPos.line + (nextEdit.endPos.col > 0 ? 1 : 0);
        auto [beginLine, endLine] = currentLines.minmaxBoundWithPadding(
            min(pos.line, nextEdit.beginPos.line),
            max(pos.line + 1, editEndLine),
            params.motionPaddingAbove, params.motionPaddingBelow);

        Lines subset = currentLines.getLineRange(beginLine, endLine);
        CursorPos localPos(pos.line - beginLine, pos.col, pos.targetCol);
        CursorPos localRangeBegin(nextEdit.beginPos.line - beginLine, nextEdit.beginPos.col);
        CursorPos localRangeEnd(nextEdit.endPos.line - beginLine, nextEdit.endPos.col);

        CursorPos subsetEnd(static_cast<int>(subset.size()) - 1,
            subset.back().effectiveSize());
        MotionBoundary subsetBoundary(subset, localRangeBegin, subsetEnd,
            beginLine > 0 || boundary.hasLinesAbove(),
            endLine <= currentLines.lastLine() || boundary.hasLinesBelow());

        auto movementResults = motionOpt.optimizeToRange(
            subset, localPos, localRangeBegin, localRangeEnd,
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
      auto nvim = oracle->simulate(initial, initialPos.line, initialPos.col, results[i].sequence.str());
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
  CursorPos initialPos(0, 0);

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
    CursorPos pos = s.getPos();
    int editsCompleted = s.getEditsCompleted();
    popCount++;

    if (ctx.isGoal(s)) {
      cerr << "  POP " << popCount << ": GOAL seq='" << s.getSequence()
           << "' effort=" << s.getEffort() << " cost=" << s.getCost() << endl;
      results.emplace_back(s.getSequence().str(), s.getRunningEffort().getEffort(config));
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

      int editEndLine = nextEdit.endPos.line + (nextEdit.endPos.col > 0 ? 1 : 0);
      auto [beginLine, endLine] = currentLines.minmaxBoundWithPadding(
          min(pos.line, nextEdit.beginPos.line),
          max(pos.line + 1, editEndLine),
          compParams.motionPaddingAbove, compParams.motionPaddingBelow);

      Lines subset = currentLines.getLineRange(beginLine, endLine);
      CursorPos localPos(pos.line - beginLine, pos.col, pos.targetCol);
      CursorPos localRangeBegin(nextEdit.beginPos.line - beginLine, nextEdit.beginPos.col);
      CursorPos localRangeEnd(nextEdit.endPos.line - beginLine, nextEdit.endPos.col);

      CursorPos subsetFirst(0, 0);
      CursorPos subsetEnd(static_cast<int>(subset.size()) - 1,
          subset.back().effectiveSize());
      MotionBoundary subsetBoundary(subset, subsetFirst, subsetEnd,
          beginLine > 0, endLine <= currentLines.lastLine());

      auto rangeResults = motionOpt.optimizeToRange(
          subset, localPos, localRangeBegin, localRangeEnd,
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
      auto nvim = oracle->simulate(initial, 0, 0, results[i].sequence.str());
      cerr << "  [" << i << "] '" << results[i].sequence << "' "
           << (nvim.lines == goal ? "OK" : "WRONG") << endl;
    }
  }
}

TEST_F(DebugTest, DISABLED_InvestigateJoinPlan) {
  // Debug the J plan computation for various cases
  cerr << "\n=== JoinPlan Debug ===" << endl;

  auto dumpJoinPlan = [&](const string& label, const Lines& initial, const Lines& goal,
                          CursorPos initialPos) {
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
        cerr << "    JOIN PLAN: seq='" << ctx.joinPlans[i]->sequence.view()
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
      {"hello", "world"}, {"hello world"}, CursorPos(0, 0));

  // Case 2: JoinLinesWithResidual — "aaa\nxxx\nccc" → "aaa bbb ccc"
  dumpJoinPlan("JoinLinesWithResidual",
      {"aaa", "xxx", "ccc"}, {"aaa bbb ccc"}, CursorPos(0, 0));

  // Case 3: JoinLinesPartialJoin — 4 lines → 2 lines
  dumpJoinPlan("JoinLinesPartialJoin",
      {"aaa", "bbb", "ccc", "ddd"}, {"aaa bbb", "ccc ddd"}, CursorPos(0, 0));

  // Case 3b: Debug motionOptimizer for PartialJoin
  cerr << "\n--- PartialJoin MotionOptimizer debug ---" << endl;
  {
    Lines buffer = {"aaa bbb", "ccc", "ddd"};
    CursorPos pos(0, 3);
    CursorPos rangeBegin(1, 3);
    CursorPos rangeEnd(2, 0);
    MotionBoundary boundary(buffer, CursorPos(0, 0), buffer.endPos());

    MotionOptimizer motionOpt(config);
    NavContext navCtx;
    auto rangeResult = motionOpt.optimizeToRange(
        buffer, pos, rangeBegin, rangeEnd,
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
  CursorPos initialPos(0, 2);

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
  CompositionSearchContext ctx(initial, CursorPos(0,0), goal, "",
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
  auto compResult = opt.optimize(initial, CursorPos(0,0), goal, CursorPos(0,0), compParams);
  cerr << compResult;

  auto oracle = make_unique<NeovimOracle>();
  for (size_t i = 0; i < compResult.results.size(); i++) {
    const auto& seq = compResult.results[i].sequence;
    auto nvim = oracle->simulate(initial, 0, 0, seq.str());
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
  CursorPos editBeginPos(0, 23);
  CursorPos editEndPos(1, 19);
  EditBoundary boundary(bufferAtEdit, editBeginPos, editEndPos);

  EditOptimizer editOpt(config);

  // Standard search
  cerr << "\n=== Standard optimizeEdit ===" << endl;
  EditResult stdResult = editOpt.optimizeEdit(
      deletedLines, insertedLines, boundary, params,
      editBeginPos.line, editBeginPos.col, CursorPos(0, 29));

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
      editBeginPos.line, editBeginPos.col, CursorPos(0, 29));

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

  EditBoundary boundary(initial, CursorPos(0, 0), initial.endPos());

  EditResult result = makeOptimizer().optimizeEdit(
      initial, goal, boundary, params,
      0, 0, CursorPos(0, 0));

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

    CursorPos editPos = fromFlatIndex(static_cast<int>(i), initial);
    auto nvim = oracle->simulate(initial, editPos.line, editPos.col, r.getSequenceString().str());
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
  EditBoundary boundary2(initial2, CursorPos(0, 0), initial2.endPos());

  EditResult result2 = makeOptimizer().optimizeEdit(
      initial2, goal2, boundary2, params,
      0, 0, CursorPos(0, 0));

  int passed2 = 0, total2 = 0;
  for (size_t i = 0; i < result2.resultCount(); i++) {
    const Result& r = result2.getResults()[i];
    if (!r.isValid()) continue;
    total2++;

    CursorPos editPos = fromFlatIndex(static_cast<int>(i), initial2);
    auto nvim = oracle->simulate(initial2, editPos.line, editPos.col, r.getSequenceString().str());
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
  CursorPos editBeginPos(0, 23);  // 'S' of Switzerland
  CursorPos editEndPos(1, 19);    // one past 'n' of even (half-open, end of buffer content)

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
      editBeginPos.line, editBeginPos.col, CursorPos(0, 29));

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
      editBeginPos.line, editBeginPos.col, CursorPos(0, 29));

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
      editBeginPos.line, editBeginPos.col, CursorPos(0, 29));

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
static pair<Lines, CursorPos> applyViaEdit(const Lines& lines, CursorPos pos, string_view cmd) {
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
    CursorPos start(0, 5);
    Range range(start, start);
    auto [editLines, editPos] = applyViaEdit(buf, start, "x");

    Lines coreLines = buf;
    CursorPos corePos = start;
    VimCore::deleteRange(coreLines, range, corePos, Mode::Normal);
    EXPECT_EQ(editLines, coreLines) << "x lines mismatch";
    EXPECT_EQ(editPos.line, corePos.line) << "x line mismatch";
    EXPECT_EQ(editPos.col, corePos.col) << "x col mismatch";
  }

  // D from (0,5): delete to end of line
  {
    CursorPos start(0, 5);
    Range range(start, CursorPos(0, static_cast<int>(buf[0].size()) - 1));
    auto [editLines, editPos] = applyViaEdit(buf, start, "D");

    Lines coreLines = buf;
    CursorPos corePos = start;
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
    CursorPos start(0, 3);
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
    CursorPos start(1, 5);
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
    CursorPos start(2, 0);
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
    CursorPos start(1, 1, 10);  // col=1 but targetCol=10
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
    CursorPos start(0, 2);
    auto [editLines, editPos] = applyViaEdit(buf, start, "J");

    EditState state(buf, start, 0, 0.0);
    EditState after = state.afterJoin(true);
    EXPECT_EQ(editLines, after.getLines()) << "J lines mismatch";
    EXPECT_EQ(editPos, after.getPos()) << "J pos mismatch";
  }

  // gJ (no space)
  {
    CursorPos start(0, 2);
    auto [editLines, editPos] = applyViaEdit(buf, start, "gJ");

    EditState state(buf, start, 0, 0.0);
    EditState after = state.afterJoin(false);
    EXPECT_EQ(editLines, after.getLines()) << "gJ lines mismatch";
    EXPECT_EQ(editPos, after.getPos()) << "gJ pos mismatch";
  }

  // J on empty next line
  {
    Lines buf2 = {"hello", "", "world"};
    CursorPos start(0, 2);
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

  struct MotionTest { string cmd; CursorPos start; CursorPos expected; };
  vector<MotionTest> tests = {
    {"h", CursorPos(0, 5), CursorPos(0, 4)},
    {"l", CursorPos(0, 5), CursorPos(0, 6)},
    {"j", CursorPos(0, 5), CursorPos(1, 5)},
    {"k", CursorPos(1, 3), CursorPos(0, 3)},
  };

  for (const auto& t : tests) {
    auto [editLines, editPos] = applyViaEdit(buf, t.start, t.cmd);
    EXPECT_EQ(editPos.line, t.expected.line) << t.cmd << " line mismatch";
    EXPECT_EQ(editPos.col, t.expected.col) << t.cmd << " col mismatch";
    EXPECT_EQ(editLines, buf) << t.cmd << " should not modify buffer";
  }
}

TEST_F(NeovimOracleDebug, DISABLED_TraceDeleteEntireLineIter20) {
  // DeleteEntireLine iter=20: jDJ produces wrong result
  // Initial: ["debceb,", "d a, a", " .ec"]
  // Goal:    ["debceb,", " .ec"]
  // Seq: jDJ → ["debceb,", ".ec"] (strips leading space from " .ec")
  cerr << "=== DeleteEntireLine iter=20 ===" << endl;

  Lines initial = {"debceb,", "d a, a", " .ec"};
  Lines goal = {"debceb,", " .ec"};

  // Run composition optimizer
  Config config = Config::uniform();
  CompositionOptimizer opt{config};
  CompositionOptimizerParams params;

  auto res = opt.optimize(initial, CursorPos(0,0), goal, CursorPos(0,0), params);
  cerr << "Results: " << res.results.size() << endl;
  for (size_t i = 0; i < res.results.size(); i++) {
    const auto& r = res.results[i];
    cerr << "  [" << i << "] seq='" << r.sequence << "' cost=" << r.keyCost << endl;
    auto nvim = oracle_->simulate(initial, 0, 0, r.sequence.str());
    cerr << "    nvim: " << nvim.lines << (nvim.lines == goal ? " OK" : " WRONG") << endl;
  }

  // Also check diffs
  auto diffs = Myers::calculate(initial, goal);
  cerr << "\nDiffs: " << diffs.size() << endl;
  for (size_t i = 0; i < diffs.size(); i++) {
    const auto& d = diffs[i];
    cerr << "  [" << i << "] begin=(" << d.beginPos.line << "," << d.beginPos.col
         << ") end=(" << d.endPos.line << "," << d.endPos.col << ")"
         << " del='" << d.deletedText << "' ins='" << d.insertedText << "'"
         << " prefix='" << d.boundary.prefix() << "' suffix='" << d.boundary.suffix() << "'"
         << " hasAbove=" << d.boundary.hasLinesAbove()
         << " hasBelow=" << d.boundary.hasLinesBelow()
         << endl;

    if (!d.isPureInsertion()) {
      EditOptimizer editOpt(config);
      EditOptimizerParams eparams;

      // Check if pure deletion
      if (d.insertedText.empty()) {
        cerr << "    Pure deletion" << endl;
        EditResult eres = editOpt.optimizeEdit(
            d.deletedLines(), {}, d.boundary, eparams);
        for (size_t j = 0; j < eres.resultCount(); j++) {
          if (eres.getResults()[j].isValid()) {
            cerr << "    pos " << j << ": '" << eres.getResults()[j].sequence
                 << "' cost=" << eres.getResults()[j].keyCost << endl;
          }
        }
      }
    }
  }
}

TEST_F(NeovimOracleDebug, DISABLED_TraceJoinLinesResidualEditOpt) {
  // Trace why jdawce<typed><Esc> is missing <BS> collapse
  // Initial: ["aaa", "xxx", "ccc"], goal: ["aaa bbb ccc"]
  // Diff: '\nxxx\nccc' -> ' bbb ccc', prefix="aaa", suffix=""
  // Edit region effective lines: ["aaa", "xxx", "ccc"]
  cerr << "=== Trace JoinLinesResidual Edit Optimizer ===" << endl;

  // Step 1: Verify what daw does at (1,0) on ["aaa", "xxx", "ccc"]
  cerr << "\n--- Step 1: daw at (1,0) ---" << endl;
  {
    Lines buf = {"aaa", "xxx", "ccc"};
    // Oracle
    auto nvim = oracle_->simulate(buf, 1, 0, "daw");
    cerr << "  Neovim daw: " << nvim.lines << " cursor=(" << nvim.row << "," << nvim.col << ")" << endl;

    // Our sim
    Lines simBuf = buf;
    CursorPos simPos(1, 0);
    Mode simMode = Mode::Normal;
    Edit::applyEdit(simBuf, simPos, simMode, Edit::parseEdits("daw")[0]);
    cerr << "  Our daw:    " << simBuf << " cursor=" << simPos << endl;
    bool match = simBuf == nvim.lines;
    cerr << "  " << (match ? "MATCH" : "MISMATCH") << endl;
  }

  // Step 2: What does de do at (1,0) on ["aaa", "", "ccc"]?
  cerr << "\n--- Step 2: de at (1,0) on ['aaa', '', 'ccc'] ---" << endl;
  {
    Lines buf = {"aaa", "", "ccc"};
    auto nvim = oracle_->simulate(buf, 1, 0, "de");
    cerr << "  Neovim de: " << nvim.lines << " cursor=(" << nvim.row << "," << nvim.col << ")" << endl;

    Lines simBuf = buf;
    CursorPos simPos(1, 0);
    Mode simMode = Mode::Normal;
    Edit::applyEdit(simBuf, simPos, simMode, Edit::parseEdits("de")[0]);
    cerr << "  Our de:    " << simBuf << " cursor=" << simPos << endl;
    bool match = simBuf == nvim.lines;
    cerr << "  " << (match ? "MATCH" : "MISMATCH") << endl;
  }

  // Step 3: What does textObjectRange return for aw at (1,0)?
  cerr << "\n--- Step 3: textObjectRange aw at (1,0) on ['aaa', 'xxx', 'ccc'] ---" << endl;
  {
    Lines buf = {"aaa", "xxx", "ccc"};
    // No boundary (full buffer)
    Range r = VimCore::textObjectRange(CursorPos(1,0), buf, false, false, 0, 0, false, false);
    cerr << "  aw range: (" << r.begin.line << "," << r.begin.col
         << ")-(" << r.end.line << "," << r.end.col << ")" << endl;

    // With hasLinesAbove=true (as the edit boundary would have)
    Range r2 = VimCore::textObjectRange(CursorPos(1,0), buf, false, false, 0, 0, true, false);
    cerr << "  aw range (hasAbove): (" << r2.begin.line << "," << r2.begin.col
         << ")-(" << r2.end.line << "," << r2.end.col << ")" << endl;
  }

  // Step 4: Run the actual edit optimizer and check all results
  cerr << "\n--- Step 4: Edit optimizer results ---" << endl;
  {
    Lines initial = {"aaa", "xxx", "ccc"};
    Lines goal = {"aaa bbb ccc"};

    // Build boundary from full buffer perspective
    CursorPos beginPos(0, 3);  // After "aaa"
    CursorPos endPos = initial.endPos();
    EditBoundary boundary(initial, beginPos, endPos);
    cerr << "  prefix='" << boundary.prefix() << "' suffix='" << boundary.suffix() << "'" << endl;
    cerr << "  hasLinesAbove=" << boundary.hasLinesAbove()
         << " hasLinesBelow=" << boundary.hasLinesBelow()
         << " hasPrefix=" << boundary.hasPrefix()
         << " hasSuffix=" << boundary.hasSuffix() << endl;

    // The edit region: content after prefix "aaa"
    // deletedLines includes newlines and content: "\nxxx\nccc" -> ["", "xxx", "ccc"]
    Lines deletedLines = {"", "xxx", "ccc"};
    Lines insertedLines = {" bbb ccc"};

    Config config = Config::uniform();
    EditOptimizer opt(config);
    EditResult res = opt.optimizeEdit(deletedLines, insertedLines, boundary, {});

    cerr << "  results: " << res.resultCount() << " total" << endl;
    int idx = 0;
    Lines effectiveLines = {"aaa", "xxx", "ccc"};
    for (int r = 0; r < static_cast<int>(deletedLines.size()); r++) {
      for (int c = 0; c < deletedLines[r].effectiveSize(); c++) {
        const Result& result = res.getResults()[idx];
        if (result.isValid()) {
          int fullRow = r + (r == 0 ? static_cast<int>(beginPos.line) : 0);
          // For row 0, col offset is beginPos.col; for others, no offset
          // Actually need to compute proper full-buffer position
          cerr << "  [" << r << "," << c << "] seq='" << result.sequence
               << "' cost=" << result.keyCost << endl;
          cerr << "    bytes:";
          for (unsigned char ch : result.sequence.view()) {
            if (ch >= 0x20 && ch < 0x7f) cerr << " '" << ch << "'";
            else cerr << " 0x" << hex << (int)ch << dec;
          }
          cerr << endl;
        }
        idx++;
      }
    }
  }
}

// ============================================================================
// Investigate d) sentence delete divergence from Neovim
// Our model stops `)` at the period; Neovim goes past it at end-of-buffer.
// ============================================================================

class NeovimOracleDebugSentence : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  static void SetUpTestSuite() { oracle = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }
};
unique_ptr<NeovimOracle> NeovimOracleDebugSentence::oracle;

TEST_F(NeovimOracleDebugSentence, DISABLED_SentenceDeleteDivergence) {
  // Failing case from EditOptimizerOutputCorrectness.SingleLine_Change iter=8
  // Source: "c bedf.", pos [0,1], full sequence "d)cge ddfecdb<Esc>"
  Lines source = {"c bedf."};

  // Step 1: What does Neovim produce for d) from [0,1]?
  auto nvimD = oracle->simulate(source, 0, 1, "d)");
  cerr << "Neovim d) from [0,1] in 'c bedf.':" << endl;
  cerr << "  lines=" << nvimD.lines << " pos=" << nvimD.row << "," << nvimD.col << endl;

  // Step 2: What does our model produce for d) from [0,1]?
  Lines modelLines = source;
  CursorPos modelPos(0, 1);
  Mode modelMode = Mode::Normal;
  string lastEdit;
  auto edits = Edit::parseEdits("d)");
  for (auto& e : edits) {
    Edit::applyEdit(modelLines, modelPos, modelMode, e, &lastEdit);
  }
  cerr << "Model d) from [0,1] in 'c bedf.':" << endl;
  cerr << "  lines=" << modelLines << " pos=" << modelPos.line << "," << modelPos.col << endl;

  // Step 3: Do they match?
  bool bufferMatch = nvimD.lines == modelLines;
  bool posMatch = (nvimD.row == modelPos.line && nvimD.col == modelPos.col);
  cerr << "Buffer match: " << bufferMatch << " CursorPos match: " << posMatch << endl;

  // Step 4: What does the full sequence produce in Neovim?
  auto nvimFull = oracle->simulate(source, 0, 1, "d)cge ddfecdb\x1b");
  cerr << "Neovim full 'd)cge ddfecdb<Esc>' from [0,1]:" << endl;
  cerr << "  lines=" << nvimFull.lines << " pos=" << nvimFull.row << "," << nvimFull.col << endl;

  // Step 5: From the Neovim post-d) state, what does cge produce?
  auto nvimCge = oracle->simulate(nvimD.lines, nvimD.row, nvimD.col, "cge ddfecdb\x1b");
  cerr << "Neovim 'cge ddfecdb<Esc>' from post-d) state:" << endl;
  cerr << "  lines=" << nvimCge.lines << " pos=" << nvimCge.row << "," << nvimCge.col << endl;

  // Step 6: From the MODEL post-d) state, what does cge produce in Neovim?
  auto nvimCge2 = oracle->simulate(modelLines, modelPos.line, modelPos.col,
                                    "cge ddfecdb\x1b");
  cerr << "Neovim 'cge ddfecdb<Esc>' from MODEL post-d) state:" << endl;
  cerr << "  lines=" << nvimCge2.lines << " pos=" << nvimCge2.row << "," << nvimCge2.col << endl;
}

// Investigate ) motion divergence in sub-buffer mode.
// The sub-buffer ) motion lands at a different column than Neovim on the full buffer
// because sentence boundaries depend on text outside the sub-buffer.
TEST_F(NeovimOracleDebugSentence, InvestigateSentenceSubBuffer) {
  // Exact failing case from SubBufferMotionCorrectness
  Lines fullBuffer = {
    "  c ,da  c,. c.bdab",
    "ba d..cbbbd.c,c,,..cdd.aa .",
    ".,bdd c,a   .bdbaa dbcbd.c,",
    ".,b,bdb cc,cd,.abcd  ,a",
    "cd dac,b a ,aa a ,. .a.bb.d.",
    " d.  ..ccd",
    ".,.ddcd.,c,,b",
    "a dbcd,ccada  .ba ,.. bb"
  };
  Lines subBuffer = {
    "cd dac,b a ,aa a ,. .a.bb.d.",
    " d.  ..ccd",
    ".,.ddcd.,c,,b",
    "a dbcd,ccada  .ba ,.. bb"
  };

  cerr << "=== Full buffer ) motion ===" << endl;
  auto nvimFull = oracle->simulate(fullBuffer, 5, 0, ")");
  cerr << "Neovim ) from full[5,0]: (" << nvimFull.row << "," << nvimFull.col << ")" << endl;

  cerr << "\n=== Sub-buffer ) motion ===" << endl;
  auto nvimSub = oracle->simulate(subBuffer, 1, 0, ")");
  cerr << "Neovim ) from sub[1,0]: (" << nvimSub.row << "," << nvimSub.col << ")" << endl;

  cerr << "\n=== Our motionSentenceNext on sub-buffer ===" << endl;
  CursorPos ourPos(1, 0);
  VimCore::motionSentenceNext(ourPos, subBuffer);
  cerr << "Our ) from sub[1,0]: (" << ourPos.line << "," << ourPos.col << ")" << endl;

  cerr << "\n=== Our motionSentenceNext on full buffer ===" << endl;
  CursorPos ourFullPos(5, 0);
  VimCore::motionSentenceNext(ourFullPos, fullBuffer);
  cerr << "Our ) from full[5,0]: (" << ourFullPos.line << "," << ourFullPos.col << ")" << endl;

  // The sub-buffer result should match Neovim's sub-buffer result
  cerr << "\n=== Comparison ===" << endl;
  cerr << "Neovim full[5,0] -> (" << nvimFull.row << "," << nvimFull.col << ") = sub("
       << (nvimFull.row - 4) << "," << nvimFull.col << ")" << endl;
  cerr << "Neovim sub[1,0]  -> (" << nvimSub.row << "," << nvimSub.col << ")" << endl;
  cerr << "Ours   sub[1,0]  -> (" << ourPos.line << "," << ourPos.col << ")" << endl;
  cerr << "Ours   full[5,0] -> (" << ourFullPos.line << "," << ourFullPos.col << ")" << endl;

  // Trace sentence boundaries in both buffers
  cerr << "\n=== Sentence boundary scan in sub-buffer ===" << endl;
  for (int l = 0; l < (int)subBuffer.size(); l++) {
    for (int c = 0; c < (int)subBuffer[l].size(); c++) {
      if (VimCore::isSentenceEndAt(subBuffer, l, c)) {
        auto [nl, nk] = VimCore::skipToSentenceStart(subBuffer, l, c);
        cerr << "  SentEnd at sub[" << l << "," << c << "] ('" << subBuffer[l][c]
             << "') -> next start (" << nl << "," << nk << ")" << endl;
      }
    }
  }

  cerr << "\n=== Sentence boundary scan in full buffer ===" << endl;
  for (int l = 0; l < (int)fullBuffer.size(); l++) {
    for (int c = 0; c < (int)fullBuffer[l].size(); c++) {
      if (VimCore::isSentenceEndAt(fullBuffer, l, c)) {
        auto [nl, nk] = VimCore::skipToSentenceStart(fullBuffer, l, c);
        cerr << "  SentEnd at full[" << l << "," << c << "] ('" << fullBuffer[l][c]
             << "') -> next start (" << nl << "," << nk << ")" << endl;
      }
    }
  }
}

// Simple test: ) should cross line boundaries to find sentence end
TEST_F(NeovimOracleDebugSentence, DISABLED_SentenceNextCrossLine) {
  Lines lines = {"hello world", "end. next sentence"};
  // From (0,0), ) should find the '.' at (1,3) and go to (1,5)
  auto nvim = oracle->simulate(lines, 0, 0, ")");
  cerr << "Nvim ) from (0,0): (" << nvim.row << "," << nvim.col << ")" << endl;

  CursorPos pos(0, 0);
  VimCore::motionSentenceNext(pos, lines);
  cerr << "Ours ) from (0,0): (" << pos.line << "," << pos.col << ")" << endl;

  // What about end of line with no sentence end?
  Lines lines2 = {"hello", "world. next"};
  auto nvim2 = oracle->simulate(lines2, 0, 0, ")");
  cerr << "Nvim ) from (0,0) on lines2: (" << nvim2.row << "," << nvim2.col << ")" << endl;

  CursorPos pos2(0, 0);
  VimCore::motionSentenceNext(pos2, lines2);
  cerr << "Ours ) from (0,0) on lines2: (" << pos2.line << "," << pos2.col << ")" << endl;

  // What about a simple single-line case?
  Lines lines3 = {"hello. world"};
  auto nvim3 = oracle->simulate(lines3, 0, 0, ")");
  cerr << "Nvim ) from (0,0) on lines3: (" << nvim3.row << "," << nvim3.col << ")" << endl;

  // No sentence end anywhere
  Lines lines4 = {"hello world", "no punct here"};
  auto nvim4 = oracle->simulate(lines4, 0, 0, ")");
  cerr << "Nvim ) from (0,0) on lines4: (" << nvim4.row << "," << nvim4.col << ")" << endl;
}

// Sentence ) and ( exhaustive stress tests moved to Motion.cpp (historical debug).

// Reproduce CompositionOptimizer/EditSize/Small crash - seed 44
TEST_F(DebugTest, DISABLED_CompositionMultiLineCrash) {
  // Was reproducing EditSize/MultiLine crash: "Edit invalid on empty line"
  // Root cause: needsKEscape k moved cursor one line too far in effective lines
  // because deleteRangeLinewise already clamped. Fixed by setting A* cursor to
  // max(0, size-2) to match replay position after dd+k.
  constexpr int DEFAULT_LINES = 15;
  constexpr int DEFAULT_AVG_LEN = 20;
  constexpr int DEFAULT_EDIT_COUNT = 5;

  for (int seed = 42; seed <= 46; seed++) {
    RandomGen::seed(seed);
    Lines initial = randomCodeBuffer(DEFAULT_LINES, DEFAULT_AVG_LEN);
    Lines goal = initial;
    for (int e = 0; e < DEFAULT_EDIT_COUNT; e++) {
      int goalSize = static_cast<int>(goal.size());
      int line = e * (goalSize - 1) / max(1, DEFAULT_EDIT_COUNT - 1);
      line = min(line, goalSize - 1);
      if (line + 1 < static_cast<int>(goal.size())) {
        goal[line] = randomWord(DEFAULT_AVG_LEN * 5 / 2);
        goal.erase(goal.begin() + line + 1);
      } else {
        goal[line] = randomWord(DEFAULT_AVG_LEN * 5 / 2);
      }
    }
    Config config = Config::uniform();
    CompositionOptimizer opt(config);
    opt.optimize(initial, {0, 0}, goal, {0, 0});
  }
}

TEST_F(DebugTest, DISABLED_CompositionEditSizeSmallCrash) {
  // Exact inputs that crash (from seed 44):
  Lines initial = {
    "e.cdc.d f..a;",
    ",fdbda fb ,a a ab.e e.daea e ,",
    "fa  .af.   ..cbd ,b, a e.bd ,",
    "eeac, b fa.,e., dadd ac,.a ec",
    "bcaed aee.. f.",
    "dc c,.,ebdce,",
    ".ffb..fc.c.d fecbedccdad cd  {",
    "}",
    "",
    "",
    ". aedff efbf",
    "edfc b ,e,aebcf.  cd. . ;",
    "dd,bff d,d,d ,",
    "ca,fcb,b f,da e,,b ,aa,   ;",
    "ed,da  ,b,d.d;"
  };
  Lines goal = {
    "e.cdbcacf..a;",
    ",fdbda fb ,a a ab.e e.daea e ,",
    "fa  .af.   ..cbd ,b, a e.bd ,",
    "eeac, b fa.,ebdccbdd ac,.a ec",
    "bcaed aee.. f.",
    "dc c,.,ebdce,",
    ".ffb..fc.c.d fecbedccdad cd  {",
    "e",
    "",
    "",
    ". cbbab efbf",
    "edfc b ,e,aebcf.  cd. . ;",
    "dd,bff d,d,d ,",
    "ca,fcb,b f,da e,,b ,aa,   ;",
    "ed,deeffb,d.d;"
  };

  cerr << "Initial: " << initial << endl;
  cerr << "Goal:    " << goal << endl;

  Config config = Config::uniform();
  CompositionOptimizer opt(config);
  auto result = opt.optimize(initial, {0, 0}, goal, {0, 0});
  cerr << "OK - nodes=" << result.stats.nodesExplored << endl;
}

// =============================================================================
// Diagnose why CompositionOptimizer benchmarks find 0 results
// =============================================================================
TEST_F(DebugTest, DISABLED_ReproduceSmallEmbeddedSentenceCrash) {
  // Reproduce the exact failing case from MultiLine_EmbeddedChange:
  // FullBuffer: be.df.\n.ee  cb  (prefix="be", suffix="cb")
  // EditRegion: .df.\n.ee
  // Sequence: d)dEdwi  b,f.f<CR>cd.a<Esc>
  // Expected prefix+result+suffix: 'be b,f.f\ncd.acb'
  // Got: 'b b,f.f\ncd.acb' (prefix 'e' eaten)

  Lines editRegion = {".df.", ".ee  "};
  Lines fullBuffer = {"be.df.", ".ee  cb"};
  EditBoundary boundary(fullBuffer, CursorPos(0, 2), CursorPos(1, 5));

  cerr << "editRegion=" << editRegion << endl;
  cerr << "boundary: hasPrefix=" << boundary.hasPrefix()
       << " hasSuffix=" << boundary.hasSuffix()
       << " leftColOffset=" << boundary.leftColOffset()
       << " rightColOffset=" << boundary.rightColOffset()
       << " hasLinesAbove=" << boundary.hasLinesAbove()
       << " hasLinesBelow=" << boundary.hasLinesBelow() << endl;

  // Step-by-step replay of the failing sequence
  Lines buf = editRegion;
  CursorPos pos(0, 0);
  Mode mode = Mode::Normal;
  string lastEditCmd;

  auto applyAndLog = [&](const string& cmd) {
    cerr << "  Before '" << cmd << "' at (" << pos.line << "," << pos.col << "): " << buf << endl;
    for (const ParsedEdit& op : Edit::parseEdits(cmd)) {
      Edit::applyEdit(buf, pos, mode, op, &lastEditCmd,
                      boundary.hasLinesBelow(), boundary.leftColOffset(),
                      boundary.rightColOffset(), boundary.hasLinesAbove());
    }
    cerr << "  After:  at (" << pos.line << "," << pos.col << "): " << buf << endl;
  };

  applyAndLog("d)");

  // Check: is cursor now in boundary region?
  cerr << "  inBoundary check: line=" << pos.line << " col=" << pos.col
       << " leftColOffset=" << boundary.leftColOffset()
       << " result=" << (pos.line == 0 && pos.col < boundary.leftColOffset()) << endl;
  cerr << "  buffer.size()=" << buf.size() << endl;

  // Now check explorer side: what range does the explorer generate for d) from (0,0)?
  cerr << "\n=== Explorer-side d) range check ===" << endl;
  CursorPos cursor(0, 0);
  int rightColOffset = boundary.rightColOffset();
  bool hasLinesBelow = boundary.hasLinesBelow();
  CursorPos endpoint = VimCore::findsentBounded(
      cursor, editRegion, true, rightColOffset, hasLinesBelow);
  cerr << "findsentBounded from (0,0): (" << endpoint.line << "," << endpoint.col << ")" << endl;
  if (endpoint != POSITION_OUTSIDE_BOUNDARY) {
    cerr << "Explorer d) range: [(" << cursor.line << "," << cursor.col << "), ("
         << endpoint.line << "," << endpoint.col << "))" << endl;
    // Apply the deletion to see what the explorer computes
    Lines explorerBuf = editRegion;
    CursorPos explorerPos = cursor;
    VimCore::deleteRange(explorerBuf, Range(cursor, endpoint), explorerPos, Mode::Normal);
    cerr << "Explorer d) result: " << explorerBuf << " pos=(" << explorerPos.line << "," << explorerPos.col << ")" << endl;
  }

  // === Oracle: verify d) behavior in multiple scenarios ===
  cerr << "\n=== Oracle d) behavior ===" << endl;
  auto oracle = std::make_unique<NeovimOracle>();

  // Case 1: d) from (0,2) on fullBuffer (the failing case)
  cerr << "Case 1: d) from (0,2) on fullBuffer" << endl;
  auto r1 = oracle->simulate(fullBuffer, 0, 2, "d)");
  cerr << "  Result: " << r1.lines << " pos=(" << r1.row << "," << r1.col << ")" << endl;

  // Case 2: d) from (0,0) on ["End.", "Start"] (cross-line test)
  cerr << "Case 2: d) from (0,0) on [\"End.\", \"Start\"]" << endl;
  auto r2 = oracle->simulate({"End.", "Start"}, 0, 0, "d)");
  cerr << "  Result: " << r2.lines << " pos=(" << r2.row << "," << r2.col << ")" << endl;

  // Case 3: d) from (0,0) on [".df.", ".ee  "] (editRegion, no prefix)
  cerr << "Case 3: d) from (0,0) on editRegion ['.df.', '.ee  ']" << endl;
  auto r3 = oracle->simulate({".df.", ".ee  "}, 0, 0, "d)");
  cerr << "  Result: " << r3.lines << " pos=(" << r3.row << "," << r3.col << ")" << endl;

  // Case 4: d) from (0,0) on single line ["Hello. World"]
  cerr << "Case 4: d) from (0,0) on single line ['Hello. World']" << endl;
  auto r4 = oracle->simulate({"Hello. World"}, 0, 0, "d)");
  cerr << "  Result: " << r4.lines << " pos=(" << r4.row << "," << r4.col << ")" << endl;

  // Case 5: d) from (0,0) on ["Hello.", "World"]
  cerr << "Case 5: d) from (0,0) on ['Hello.', 'World']" << endl;
  auto r5 = oracle->simulate({"Hello.", "World"}, 0, 0, "d)");
  cerr << "  Result: " << r5.lines << " pos=(" << r5.row << "," << r5.col << ")" << endl;

  // Case 6: d) from (0,0) on ["a.", "b"]
  cerr << "Case 6: d) from (0,0) on ['a.', 'b']" << endl;
  auto r6 = oracle->simulate({"a.", "b"}, 0, 0, "d)");
  cerr << "  Result: " << r6.lines << " pos=(" << r6.row << "," << r6.col << ")" << endl;

  // Case 7: d) from (0,0) on [".", "b"]
  cerr << "Case 7: d) from (0,0) on ['.', 'b']" << endl;
  auto r7 = oracle->simulate({".", "b"}, 0, 0, "d)");
  cerr << "  Result: " << r7.lines << " pos=(" << r7.row << "," << r7.col << ")" << endl;

  // Now check interpreter for these same cases (no boundary context)
  cerr << "\n=== Interpreter d) behavior (no boundary) ===" << endl;
  auto interpTest = [](const string& cmd, const Lines& lines, int row, int col) {
    Lines buf = lines;
    CursorPos pos(row, col);
    Mode mode = Mode::Normal;
    string lastEdit;
    for (const ParsedEdit& op : Edit::parseEdits(cmd)) {
      Edit::applyEdit(buf, pos, mode, op, &lastEdit, false, 0, 0, false);
    }
    cerr << "  Result: " << buf << " pos=(" << pos.line << "," << pos.col << ")" << endl;
  };

  cerr << "Case 1 interp: d) from (0,2) on fullBuffer" << endl;
  interpTest("d)", fullBuffer, 0, 2);
  cerr << "Case 2 interp: d) from (0,0) on ['End.', 'Start']" << endl;
  interpTest("d)", {"End.", "Start"}, 0, 0);
  cerr << "Case 3 interp: d) from (0,0) on ['.df.', '.ee  ']" << endl;
  interpTest("d)", {".df.", ".ee  "}, 0, 0);
  cerr << "Case 5 interp: d) from (0,0) on ['Hello.', 'World']" << endl;
  interpTest("d)", {"Hello.", "World"}, 0, 0);
  cerr << "Case 6 interp: d) from (0,0) on ['a.', 'b']" << endl;
  interpTest("d)", {"a.", "b"}, 0, 0);
  cerr << "Case 7 interp: d) from (0,0) on ['.', 'b']" << endl;
  interpTest("d)", {".", "b"}, 0, 0);

  // d( oracle tests — check exclusive-linewise adjustment for backward motions
  // The exclusive end for d( is the cursor position (the higher end).
  // When cursor.col == 0, the same adjustment should apply.
  cerr << "\n=== Oracle d( behavior ===" << endl;

  auto oracleAndInterp = [&](const string& cmd, const Lines& lines, int row, int col) {
    auto nv = oracle->simulate(lines, row, col, cmd);
    cerr << "  Oracle:  " << nv.lines << " pos=(" << nv.row << "," << nv.col << ")" << endl;
    interpTest(cmd, lines, row, col);
  };

  // Both at col 0 → linewise (both should merge)
  cerr << "d( from (1,0) on ['End.', 'Start'] (both col 0 → linewise)" << endl;
  oracleAndInterp("d(", {"End.", "Start"}, 1, 0);

  // Cursor not at col 0 → standard (same line, no crossing)
  cerr << "d( from (1,3) on ['End.', 'Start'] (same line motion)" << endl;
  oracleAndInterp("d(", {"End.", "Start"}, 1, 3);

  // Both at col 0 → linewise
  cerr << "d( from (1,0) on ['Hello.', 'World'] (both col 0)" << endl;
  oracleAndInterp("d(", {"Hello.", "World"}, 1, 0);

  // KEY TEST: cursor at col 0, endpoint at non-zero col
  // ( from (1,0) on ["End. xyz", "abc"] goes to (0,5) — start of "xyz" sentence
  // exclusive end = cursor = (1,0) at col 0, begin = (0,5) NOT at col 0
  // Should Vim's exclusive-linewise adjustment apply?
  cerr << "d( from (1,0) on ['End. xyz', 'abc'] (cursor col 0, endpoint col 5)" << endl;
  oracleAndInterp("d(", {"End. xyz", "abc"}, 1, 0);

  // Another test: cursor at col 0, but endpoint also at col 0
  cerr << "d( from (2,0) on ['End.', '', 'Start'] (blank line paragraph)" << endl;
  oracleAndInterp("d(", {"End.", "", "Start"}, 2, 0);

  // d} tests for completeness — verify d} still matches oracle
  cerr << "\n=== d} behavior ===" << endl;
  cerr << "d} from (0,0) on ['abc', '', 'def']" << endl;
  oracleAndInterp("d}", {"abc", "", "def"}, 0, 0);
  cerr << "d} from (0,2) on ['abc', '', 'def']" << endl;
  oracleAndInterp("d}", {"abc", "", "def"}, 0, 2);

  // d{ tests
  cerr << "\n=== d{ behavior ===" << endl;
  cerr << "d{ from (2,0) on ['abc', '', 'def']" << endl;
  oracleAndInterp("d{", {"abc", "", "def"}, 2, 0);
  cerr << "d{ from (2,2) on ['abc', '', 'def']" << endl;
  oracleAndInterp("d{", {"abc", "", "def"}, 2, 2);
}

TEST_F(DebugTest, DISABLED_DiagnoseCompBenchFound0) {
  // Test all 5 seeds that benchmarks use (42..46) to see which find 0 results
  constexpr int DEFAULT_LINES = 15;
  constexpr int DEFAULT_AVG_LEN = 20;
  constexpr int DEFAULT_EDIT_COUNT = 5;

  for (int seed = 42; seed <= 46; seed++) {
    RandomGen::seed(seed);
    Lines initial = randomCodeBuffer(DEFAULT_LINES, DEFAULT_AVG_LEN);
    Lines goal = initial;
    for (int e = 0; e < DEFAULT_EDIT_COUNT; e++) {
      int line = e * (DEFAULT_LINES - 1) / max(1, DEFAULT_EDIT_COUNT - 1);
      int len = max(1, static_cast<int>(initial[line].size()));
      goal[line] = randomWord(len);
      if (goal[line] == initial[line]) goal[line] = "changed";
    }

    CompositionOptimizerParams params;
    CompositionOptimizer opt(config);
    auto result = opt.optimize(initial, {0,0}, goal, {0,0}, params);
    cerr << "seed=" << seed
         << " results=" << result.results.size()
         << " stats.resultsFound=" << result.stats.resultsFound
         << " nodes=" << result.stats.nodesExplored
         << " stop=" << static_cast<int>(result.stats.stopReason) << endl;
  }
}

// ============================================================================
// Investigate ca( mask mismatch from BracketQuoteContextTest.Random_FullyRandom
// ============================================================================
//
// Failure: mask says ca( valid from cols 0-7, but Neovim disagrees.
// Line: e[ d{(")bg)b "ccbf""
// Question: does Neovim's a( search forward when cursor is outside ()?
TEST_F(NeovimOracleDebug, DISABLED_InvestigateCaParenMask) {
  string line = "e[ d{(\")bg)b \"ccbf\"\"";
  cerr << "Line: '" << line << "' (len=" << line.size() << ")" << endl;
  cerr << "Cols: ";
  for (int i = 0; i < (int)line.size(); i++)
    cerr << "[" << i << "]=" << line[i] << " ";
  cerr << endl << endl;

  // Test ca( from every column — does Neovim find the () pair?
  cerr << "=== ca( from each column (replace with 'X') ===" << endl;
  for (int col = 0; col < (int)line.size(); col++) {
    auto r = oracle_->simulate({line}, 0, col, "ca(X\x1b");
    cerr << "  col " << col << " ('" << line[col] << "'): '"
         << r.lines[0] << "' cursor=(" << r.row << "," << r.col << ")" << endl;
  }

  // Test ci( from every column
  cerr << endl << "=== ci( from each column (replace with 'X') ===" << endl;
  for (int col = 0; col < (int)line.size(); col++) {
    auto r = oracle_->simulate({line}, 0, col, "ci(X\x1b");
    cerr << "  col " << col << " ('" << line[col] << "'): '"
         << r.lines[0] << "' cursor=(" << r.row << "," << r.col << ")" << endl;
  }

  // Simpler test: does a( forward-search at all?
  cerr << endl << "=== Simple forward-search test ===" << endl;
  auto r1 = oracle_->simulate({"abc(def)ghi"}, 0, 0, "ca(X\x1b");
  cerr << "  'abc(def)ghi' col 0: '" << r1.lines[0] << "'" << endl;
  auto r2 = oracle_->simulate({"abc(def)ghi"}, 0, 2, "ca(X\x1b");
  cerr << "  'abc(def)ghi' col 2: '" << r2.lines[0] << "'" << endl;
  auto r3 = oracle_->simulate({"abc(def)ghi"}, 0, 3, "ca(X\x1b");
  cerr << "  'abc(def)ghi' col 3: '" << r3.lines[0] << "'" << endl;
  auto r4 = oracle_->simulate({"abc(def)ghi"}, 0, 5, "ca(X\x1b");
  cerr << "  'abc(def)ghi' col 5: '" << r4.lines[0] << "'" << endl;

  // With nested: does a( pick innermost from outside?
  cerr << endl << "=== Nested parens ===" << endl;
  auto r5 = oracle_->simulate({"a(b(c)d)e"}, 0, 0, "ca(X\x1b");
  cerr << "  'a(b(c)d)e' col 0: '" << r5.lines[0] << "'" << endl;
  auto r6 = oracle_->simulate({"a(b(c)d)e"}, 0, 1, "ca(X\x1b");
  cerr << "  'a(b(c)d)e' col 1: '" << r6.lines[0] << "'" << endl;
  auto r7 = oracle_->simulate({"a(b(c)d)e"}, 0, 4, "ca(X\x1b");
  cerr << "  'a(b(c)d)e' col 4: '" << r7.lines[0] << "'" << endl;

  // Key question: do unmatched brackets before () block forward search?
  cerr << endl << "=== Unmatched brackets before () ===" << endl;
  auto u1 = oracle_->simulate({"[a(bc)d"}, 0, 0, "ca(X\x1b");
  cerr << "  '[a(bc)d' col 0: '" << u1.lines[0] << "'" << endl;
  auto u2 = oracle_->simulate({"{a(bc)d"}, 0, 0, "ca(X\x1b");
  cerr << "  '{a(bc)d' col 0: '" << u2.lines[0] << "'" << endl;
  auto u3 = oracle_->simulate({"xa(bc)d"}, 0, 0, "ca(X\x1b");
  cerr << "  'xa(bc)d' col 0: '" << u3.lines[0] << "'" << endl;
  auto u4 = oracle_->simulate({"x[a(bc)d"}, 0, 0, "ca(X\x1b");
  cerr << "  'x[a(bc)d' col 0: '" << u4.lines[0] << "'" << endl;
  auto u5 = oracle_->simulate({"x[(bc)d"}, 0, 0, "ca(X\x1b");
  cerr << "  'x[(bc)d' col 0: '" << u5.lines[0] << "'" << endl;

  // What about unmatched ) after the pair?
  cerr << endl << "=== Unmatched ) after pair ===" << endl;
  auto v1 = oracle_->simulate({"a(bc)d)e"}, 0, 0, "ca(X\x1b");
  cerr << "  'a(bc)d)e' col 0 (extra )): '" << v1.lines[0] << "'" << endl;
  auto v2 = oracle_->simulate({"a(bc))de"}, 0, 0, "ca(X\x1b");
  cerr << "  'a(bc))de' col 0 (extra ) right after): '" << v2.lines[0] << "'" << endl;

  // Inside the pair still works?
  cerr << endl << "=== Inside pair with unmatched brackets outside ===" << endl;
  auto w1 = oracle_->simulate({"[a(bc)d"}, 0, 3, "ca(X\x1b");
  cerr << "  '[a(bc)d' col 3 (inside): '" << w1.lines[0] << "'" << endl;
  auto w2 = oracle_->simulate({"{a(bc)d"}, 0, 3, "ca(X\x1b");
  cerr << "  '{a(bc)d' col 3 (inside): '" << w2.lines[0] << "'" << endl;

  // Does an EXTRA ) after the pair block forward search from before?
  cerr << endl << "=== Extra ) after pair — forward search from col 0 ===" << endl;
  auto x1 = oracle_->simulate({"a(bc)d)e"}, 0, 0, "ca(X\x1b");
  cerr << "  'a(bc)d)e' col 0: '" << x1.lines[0] << "'" << endl;  // extra ) after
  auto x2 = oracle_->simulate({"a(b)c)de"}, 0, 0, "ca(X\x1b");
  cerr << "  'a(b)c)de' col 0: '" << x2.lines[0] << "'" << endl;  // extra ) after
  auto x3 = oracle_->simulate({"a(b)c)"}, 0, 0, "ca(X\x1b");
  cerr << "  'a(b)c)' col 0: '" << x3.lines[0] << "'" << endl;

  // Stripped-down version of the failing line — is it the " inside () ?
  cerr << endl << "=== Does \" inside () affect forward search? ===" << endl;
  auto y1 = oracle_->simulate({"a(\")b)c"}, 0, 0, "ca(X\x1b");
  cerr << "  'a(\")b)c' col 0: '" << y1.lines[0] << "'" << endl;
  auto y2 = oracle_->simulate({"a(x)b)c"}, 0, 0, "ca(X\x1b");
  cerr << "  'a(x)b)c' col 0: '" << y2.lines[0] << "'" << endl;
  auto y3 = oracle_->simulate({"a(\")bc"}, 0, 0, "ca(X\x1b");
  cerr << "  'a(\")bc' col 0: '" << y3.lines[0] << "'" << endl;
  auto y4 = oracle_->simulate({"a(x)bc"}, 0, 0, "ca(X\x1b");
  cerr << "  'a(x)bc' col 0: '" << y4.lines[0] << "'" << endl;

  // Progressive build-up of the failing line
  cerr << endl << "=== Progressive build-up ===" << endl;
  auto z1 = oracle_->simulate({"(x)b)"}, 0, 0, "ca(X\x1b");
  cerr << "  '(x)b)' col 0 (on open paren): '" << z1.lines[0] << "'" << endl;
  auto z1b = oracle_->simulate({"a(x)b)"}, 0, 0, "ca(X\x1b");
  cerr << "  'a(x)b)' col 0 (before pair): '" << z1b.lines[0] << "'" << endl;
  auto z2 = oracle_->simulate({"a(\")b)"}, 0, 0, "ca(X\x1b");
  cerr << "  'a(\")b)' col 0: '" << z2.lines[0] << "'" << endl;
  auto z3 = oracle_->simulate({"a(\")bg)"}, 0, 0, "ca(X\x1b");
  cerr << "  'a(\")bg)' col 0: '" << z3.lines[0] << "'" << endl;
  auto z4 = oracle_->simulate({"a(\")bg)b"}, 0, 0, "ca(X\x1b");
  cerr << "  'a(\")bg)b' col 0: '" << z4.lines[0] << "'" << endl;

  // =========================================================================
  // Theory: Neovim's findmatchlimit counts " on the line.
  // Even count → enable string-skipping (brackets inside strings are skipped).
  // Odd count → disable string-skipping (all brackets match normally).
  // =========================================================================
  cerr << endl << "=== Quote-count theory ===" << endl;

  // 1 quote (odd) → no string-skipping → ) matches
  auto q1 = oracle_->simulate({"a(\")bc"}, 0, 0, "ca(X\x1b");
  cerr << "  'a(\")bc' col 0 [1 quote, odd]: '" << q1.lines[0] << "'" << endl;

  // 2 quotes (even) → string-skipping → ) inside "..." is skipped
  auto q2 = oracle_->simulate({"a(\")bc\""}, 0, 0, "ca(X\x1b");
  cerr << "  'a(\")bc\"' col 0 [2 quotes, even]: '" << q2.lines[0] << "'" << endl;

  // 3 quotes (odd) → no string-skipping
  auto q3 = oracle_->simulate({"a(\")bc\"d\""}, 0, 0, "ca(X\x1b");
  cerr << "  'a(\")bc\"d\"' col 0 [3 quotes, odd]: '" << q3.lines[0] << "'" << endl;

  // 4 quotes (even) → string-skipping
  auto q4 = oracle_->simulate({"a(\")bc\"d\"e\""}, 0, 0, "ca(X\x1b");
  cerr << "  'a(\")bc\"d\"e\"' col 0 [4 quotes, even]: '" << q4.lines[0] << "'" << endl;

  // Even quotes but ) OUTSIDE the string pairs — should still work?
  // "x" a(bc)d "y" — quotes are balanced, () is outside strings
  auto q5 = oracle_->simulate({"\"x\"a(bc)d\"y\""}, 0, 3, "ca(X\x1b");
  cerr << "  '\"x\"a(bc)d\"y\"' col 3 [4 quotes, () outside strings]: '"
       << q5.lines[0] << "'" << endl;

  // Same but from col 0 (forward search)
  auto q5b = oracle_->simulate({"\"x\"a(bc)d\"y\""}, 0, 0, "ca(X\x1b");
  cerr << "  '\"x\"a(bc)d\"y\"' col 0 [4 quotes, fwd search]: '"
       << q5b.lines[0] << "'" << endl;

  // Even quotes, ) between first " pair
  auto q6 = oracle_->simulate({"a\"(bc)\"d"}, 0, 0, "ca(X\x1b");
  cerr << "  'a\"(bc)\"d' col 0 [2 quotes, () inside string]: '"
       << q6.lines[0] << "'" << endl;

  // Verify: col on ( directly for even-quote case
  auto q7 = oracle_->simulate({"a(\")bc\""}, 0, 1, "ca(X\x1b");
  cerr << "  'a(\")bc\"' col 1 on ( [2 quotes]: '" << q7.lines[0] << "'" << endl;

  // What about single quotes?
  auto q8 = oracle_->simulate({"a(')bc"}, 0, 0, "ca(X\x1b");
  cerr << "  'a(')bc' col 0 [1 single quote]: '" << q8.lines[0] << "'" << endl;
  auto q9 = oracle_->simulate({"a(')bc'"}, 0, 0, "ca(X\x1b");
  cerr << "  'a(')bc'' col 0 [2 single quotes]: '" << q9.lines[0] << "'" << endl;

  // Zero quotes — normal matching
  auto q0 = oracle_->simulate({"a(bc)d"}, 0, 0, "ca(X\x1b");
  cerr << "  'a(bc)d' col 0 [0 quotes]: '" << q0.lines[0] << "'" << endl;

  // =========================================================================
  // Zone-matching theory: both ( and ) must be in same "string zone"
  // Zone = even/odd count of " before the bracket
  // =========================================================================
  cerr << endl << "=== Zone-matching verification ===" << endl;

  // ( outside string, ) inside string → no match
  // a(")b" : ( at 1 [0 " before=even], ) at 3 [1 " before=odd] → different zones
  auto zm1 = oracle_->simulate({"a(\")b\""}, 0, 0, "ca(X\x1b");
  cerr << "  'a(\")b\"' col 0 [( outside, ) inside]: '" << zm1.lines[0] << "'" << endl;

  // ( inside string, ) inside string → same zone → match
  // "a(bc)d" : ( at 2 [1 " before=odd], ) at 5 [1 " before=odd] → same zone
  auto zm2 = oracle_->simulate({"\"a(bc)d\""}, 0, 0, "ca(X\x1b");
  cerr << "  '\"a(bc)d\"' col 0 [( inside, ) inside]: '" << zm2.lines[0] << "'" << endl;

  // ( inside string, ) outside string → no match
  // "a(b"c)d : ( at 2 [1 " before=odd], ) at 5 [2 " before=even] → different zones
  auto zm3 = oracle_->simulate({"\"a(b\"c)d"}, 0, 0, "ca(X\x1b");
  cerr << "  '\"a(b\"c)d' col 0 [( inside, ) outside]: '" << zm3.lines[0] << "'" << endl;

  // Does forward search for ( also respect zones?
  // "(b)"c(d)e : first ( at 1 is inside [1 " before], second ( at 5 is outside [2 " before]
  // If fwd search skips inside-string (, it finds ( at 5 and matches ) at 7
  // If fwd search doesn't skip, it finds ( at 1 and... ) at 3 same zone → match
  auto zm4 = oracle_->simulate({"\"(b)\"c(d)e"}, 0, 0, "ca(X\x1b");
  cerr << "  '\"(b)\"c(d)e' col 0 [fwd search: skip or not?]: '"
       << zm4.lines[0] << "'" << endl;

  // Does bracket type matter for string-skipping?
  // Even quotes, ca[ with ] inside string
  auto bt1 = oracle_->simulate({"a[\"]b\""}, 0, 0, "ca[X\x1b");
  cerr << "  'a[\"]b\"' col 0 ca[ [2 quotes]: '" << bt1.lines[0] << "'" << endl;
  // Even quotes, ca{ with } inside string
  auto bt2 = oracle_->simulate({"a{\"}b\""}, 0, 0, "ca{X\x1b");
  cerr << "  'a{\"}b\"' col 0 ca{ [2 quotes]: '" << bt2.lines[0] << "'" << endl;

  // Escaped quotes: does \" count as a quote for zone detection?
  // If backslash-escaped quotes don't count: \"a(")b → 1 real quote → odd → works
  // If they do count: \"a(")b → 2 quotes → even → might fail
  auto esc1 = oracle_->simulate({"\\\"a(\")b"}, 0, 2, "ca(X\x1b");
  cerr << "  '\\\"a(\")b' col 2 [escaped + real quote]: '"
       << esc1.lines[0] << "'" << endl;

  // =========================================================================
  // Vim help says: "unless the number of parens/braces in a line is uneven"
  // Test: does ODD bracket count override string-skipping?
  // =========================================================================
  cerr << endl << "=== Odd bracket count override? ===" << endl;

  // 2 quotes (even), 3 brackets (odd): (a"b))"
  // If bracket count matters: odd brackets → disable string-skipping → match
  // If only quote count matters: even quotes → string-skipping active → no match
  auto ob1 = oracle_->simulate({"(a\"b))\""}, 0, 0, "ca(X\x1b");
  cerr << "  '(a\"b))\"' col 0 [2 quotes, 3 brackets]: '" << ob1.lines[0] << "'" << endl;

  // 2 quotes (even), 1 bracket (odd): (a"b"
  auto ob2 = oracle_->simulate({"(a\"b\""}, 0, 0, "ca(X\x1b");
  cerr << "  '(a\"b\"' col 0 [2 quotes, 1 bracket]: '" << ob2.lines[0] << "'" << endl;

  // 2 quotes, 4 brackets (even): (a"(b)"c)
  auto ob3 = oracle_->simulate({"(a\"(b)\"c)"}, 0, 0, "ca(X\x1b");
  cerr << "  '(a\"(b)\"c)' col 0 [2 quotes, 4 brackets]: '" << ob3.lines[0] << "'" << endl;

  // 4 quotes, 3 brackets (odd): "("a"b)
  auto ob4 = oracle_->simulate({"\"(\"a\"b)"}, 0, 0, "ca(X\x1b");
  cerr << "  '\"(\"a\"b)' col 0 [4 quotes(!), 3 brackets]: '" << ob4.lines[0] << "'" << endl;
  // Note: 4 quotes not 3: " at 0, " at 2, " at 4, but wait...
  // "("a"b) has " at 0, 2, 4 → 3 quotes (odd) → string detection disabled
  // So this should work regardless of bracket count

  // Let me be precise: 2 quotes, 3 () brackets
  // a("b)c)"
  auto ob5 = oracle_->simulate({"a(\"b)c)\""}, 0, 0, "ca(X\x1b");
  cerr << "  'a(\"b)c)\"' col 0 [2 quotes, 3 () brackets]: '" << ob5.lines[0] << "'" << endl;
}

TEST_F(NeovimOracleDebug, DISABLED_TraceDbDwInsertMismatch) {
  Lines initial = {"hello world"};
  CursorPos initialPos(0, 0);
  const string seq = "$dbdwi there<Esc>";

  cerr << "=== Sequence mismatch trace ===" << endl;
  cerr << "Initial: " << initial << " pos=(" << initialPos.line << "," << initialPos.col << ")" << endl;
  cerr << "Sequence: '" << seq << "'" << endl;

  auto nvimFull = oracle_->simulate(initial, initialPos.line, initialPos.col, seq);
  cerr << "Neovim full: " << nvimFull.lines
       << " pos=(" << nvimFull.row << "," << nvimFull.col << ")" << endl;

  Lines interp = initial;
  CursorPos pos = initialPos;
  Mode mode = Mode::Normal;
  string lastEdit;
  auto edits = Edit::parseEdits(seq);
  string prefix;
  for (int i = 0; i < static_cast<int>(edits.size()); i++) {
    const ParsedEdit& op = edits[i];
    prefix += string(op.edit);
    cerr << "  step[" << i << "] edit='" << op.edit << "' count="
         << op.effectiveCount() << " mode=" << (mode == Mode::Normal ? "N" : "I") << endl;
    Edit::applyEdit(interp, pos, mode, op, &lastEdit);
    cerr << "    interp -> " << interp
         << " pos=(" << pos.line << "," << pos.col << ") mode="
         << (mode == Mode::Normal ? "N" : "I") << endl;
    auto nvimStep = oracle_->simulate(initial, initialPos.line, initialPos.col, prefix);
    cerr << "    nvim(pfx)-> " << nvimStep.lines
         << " pos=(" << nvimStep.row << "," << nvimStep.col << ")" << endl;
  }
}

TEST_F(NeovimOracleDebug, DISABLED_TraceAutoindentCountedCcMismatch) {
  Lines initial = {"    aaa", "    bbb", "    ccc"};

  cerr << "=== plain } / { probe ===" << endl;
  {
    auto nvimR = oracle_->simulate(initial, 1, 1, "}");
    auto nvimL = oracle_->simulate(initial, 2, 0, "{");
    cerr << "  nvim '}' from (1,1): pos=(" << nvimR.row << "," << nvimR.col << ")" << endl;
    cerr << "  nvim '{' from (2,0): pos=(" << nvimL.row << "," << nvimL.col << ")" << endl;

    CursorPos r(1, 1), l(2, 0);
    VimCore::motionParagraphNext(r, initial);
    VimCore::motionParagraphPrev(l, initial);
    cerr << "  ours  '}' from (1,1): pos=(" << r.line << "," << r.col << ")" << endl;
    cerr << "  ours  '{' from (2,0): pos=(" << l.line << "," << l.col << ")" << endl;
  }

  cerr << "\n=== d} probe on line 1 ===" << endl;
  for (int col = 0; col <= 6; col++) {
    auto nvim = oracle_->simulate(initial, 1, col, "d}");
    Lines interp = initial;
    CursorPos pos(1, col);
    Mode mode = Mode::Normal;
    string lastEdit;
    Edit::applyEdit(interp, pos, mode, ParsedEdit("d}"), &lastEdit);
    cerr << "  col " << col
         << " nvim=(" << nvim.row << "," << nvim.col << ") '" << nvim.lines[0] << "'"
         << " interp=(" << pos.line << "," << pos.col << ") '" << interp[0] << "'"
         << endl;
  }

  cerr << "\n=== c} probe on line 0 ===" << endl;
  for (int col = 1; col <= 4; col++) {
    auto nvim = oracle_->simulate(initial, 0, col, "c}     xxx<Esc>");
    Lines interp = initial;
    CursorPos pos(0, col);
    Mode mode = Mode::Normal;
    string lastEdit;
    for (const auto& op : Edit::parseEdits("c}     xxx<Esc>")) {
      Edit::applyEdit(interp, pos, mode, op, &lastEdit);
    }
    cerr << "  col " << col
         << " nvim=(" << nvim.row << "," << nvim.col << ") '" << nvim.lines[0] << "'"
         << " interp=(" << pos.line << "," << pos.col << ") '" << interp[0] << "'"
         << endl;

    auto nvimAlt = oracle_->simulate(initial, 0, col, "d}i    xxx<Esc>");
    Lines interpAlt = initial;
    CursorPos posAlt(0, col);
    Mode modeAlt = Mode::Normal;
    string lastAlt;
    for (const auto& op : Edit::parseEdits("d}i    xxx<Esc>")) {
      Edit::applyEdit(interpAlt, posAlt, modeAlt, op, &lastAlt);
    }
    cerr << "    alt d}i: nvim=(" << nvimAlt.row << "," << nvimAlt.col
         << ") '" << nvimAlt.lines[0] << "'"
         << " interp=(" << posAlt.line << "," << posAlt.col << ") '"
         << interpAlt[0] << "'" << endl;
  }

  cerr << "\n=== d( probe on line 2 ===" << endl;
  for (int col = 0; col <= 6; col++) {
    auto nvim = oracle_->simulate(initial, 2, col, "d(");
    Lines interp = initial;
    CursorPos pos(2, col);
    Mode mode = Mode::Normal;
    string lastEdit;
    Edit::applyEdit(interp, pos, mode, ParsedEdit("d("), &lastEdit);
    cerr << "  col " << col
         << " nvim=(" << nvim.row << "," << nvim.col << ") '" << nvim.lines[0] << "'"
         << " interp=(" << pos.line << "," << pos.col << ") '" << interp[0] << "'"
         << endl;
  }

  auto dumpLines = [&](const char* tag, const Lines& lines) {
    cerr << "    " << tag << " lines(" << lines.size() << "):";
    for (int i = 0; i < static_cast<int>(lines.size()); i++) {
      cerr << " [" << i << "]='" << lines[i] << "'";
    }
    cerr << endl;
  };

  auto traceOne = [&](CursorPos start, const string& seq) {
    cerr << "\n--- start=(" << start.line << "," << start.col << ") seq='" << seq << "' ---" << endl;
    auto nvim = oracle_->simulate(initial, start.line, start.col, seq);
    cerr << "  nvim: " << nvim.lines << " pos=(" << nvim.row << "," << nvim.col << ")" << endl;

    Lines interp = initial;
    CursorPos pos = start;
    Mode mode = Mode::Normal;
    string lastEdit;
    auto edits = Edit::parseEdits(seq);
    for (const ParsedEdit& op : edits) {
      Edit::applyEdit(interp, pos, mode, op, &lastEdit);
    }
    cerr << "  interp: " << interp << " pos=(" << pos.line << "," << pos.col << ") mode="
         << (mode == Mode::Normal ? "N" : "I") << endl;
  };

  traceOne(CursorPos(1, 1), "d}ce     xxx<Esc>");
  traceOne(CursorPos(1, 2), "d}ce     xxx<Esc>");
  traceOne(CursorPos(1, 3), "d}ce     xxx<Esc>");
  traceOne(CursorPos(1, 4), "d}ce     xxx<Esc>");
  traceOne(CursorPos(2, 0), "d(ce     xxx<Esc>");
  traceOne(CursorPos(2, 0), "d(ce    xxx<Esc>");

  cerr << "\n=== step trace for start=(1,1), seq='d}ce     xxx<Esc>' ===" << endl;
  {
    CursorPos start(1, 1);
    const string seq = "d}ce     xxx<Esc>";
    Lines interp = initial;
    CursorPos pos = start;
    Mode mode = Mode::Normal;
    string lastEdit;
    auto edits = Edit::parseEdits(seq);
    string prefix;
    for (int i = 0; i < static_cast<int>(edits.size()); i++) {
      const ParsedEdit& op = edits[i];
      prefix += string(op.edit);
      cerr << "  step[" << i << "] op='" << op.edit << "' mode="
           << (mode == Mode::Normal ? "N" : "I") << endl;
      Edit::applyEdit(interp, pos, mode, op, &lastEdit);
      auto nvimStep = oracle_->simulate(initial, start.line, start.col, prefix);
      dumpLines("interp", interp);
      cerr << "      pos=(" << pos.line << "," << pos.col
           << ") mode=" << (mode == Mode::Normal ? "N" : "I") << endl;
      dumpLines("nvim  ", nvimStep.lines);
      cerr << "      pos=(" << nvimStep.row << "," << nvimStep.col << ")" << endl;
    }
  }

  cerr << "\n=== optimizeEdit sequences (AutoindentLinewise_CountedCC) ===" << endl;
  {
    Lines goal = {"    xxx"};
    EditBoundary boundary(initial, CursorPos(0, 0), initial.endPos());
    Config config = Config::uniform();
    EditOptimizer opt(config);
    EditResult res = opt.optimizeEdit(initial, goal, boundary, EditOptimizerParams{}.withMaxResults(INT_MAX));

    int idx = 0;
    for (int r = 0; r < static_cast<int>(initial.size()); r++) {
      for (int c = 0; c < initial[r].effectiveSize(); c++) {
        const Result& rr = res.getResults()[idx++];
        if (!rr.isValid()) continue;
        cerr << "  pos=(" << r << "," << c << ") seq='" << rr.sequence << "'" << endl;
        cerr << "    bytes:";
        for (unsigned char ch : rr.sequence.view()) {
          if (ch >= 0x20 && ch < 0x7f) cerr << " '" << ch << "'";
          else cerr << " 0x" << hex << static_cast<int>(ch) << dec;
        }
        cerr << endl;
      }
    }
  }
}

TEST_F(NeovimOracleDebug, DISABLED_TraceRemainingCompositionMismatches) {
  struct Case {
    Lines initial;
    CursorPos start;
    string seq;
    Lines goal;
  };

  vector<Case> cases = {
      {
          {" ff,d", "edd, e,efa", "  b,."},
          CursorPos(0, 2),
          "}xdwi ff<Esc>",
          {" ff,d", "edd, e,efa", "  bff"},
      },
      {
          {",ccb cb", "c b,a", "f ,c.f"},
          CursorPos(0, 0),
          "dEce edec<Esc> <C-d>D.dwi fbe<Esc>",
          {"edec", "c b,a", "ffbe"},
      },
      {
          {"dcd. ,", "bed cabfca", "a  ,c"},
          CursorPos(0, 0),
          "C feafe<Esc> <C-d>D..dwi bade<Esc>",
          {"feafe", "bed cabfca", "abade"},
      },
  };

  auto dump = [&](const char* tag, const Lines& lines, int row, int col, Mode mode) {
    cerr << "  " << tag << " lines(" << lines.size() << "):";
    for (int i = 0; i < static_cast<int>(lines.size()); i++) {
      cerr << " [" << i << "]='" << lines[i] << "'";
    }
    cerr << " pos=(" << row << "," << col << ") mode=" << (mode == Mode::Normal ? "N" : "I") << endl;
  };

  for (int ci = 0; ci < static_cast<int>(cases.size()); ci++) {
    const auto& c = cases[ci];
    cerr << "\n=== case " << ci << " ===" << endl;
    cerr << "seq: '" << c.seq << "' start=(" << c.start.line << "," << c.start.col << ")" << endl;

    auto nvimFull = oracle_->simulate(c.initial, c.start.line, c.start.col, c.seq);
    cerr << "goal: " << c.goal << endl;
    cerr << "nvim full: " << nvimFull.lines << " pos=(" << nvimFull.row << "," << nvimFull.col << ")" << endl;
    if (ci == 0) {
      auto alt1 = oracle_->simulate(c.initial, c.start.line, c.start.col, "}xcwff<Esc>");
      auto alt2 = oracle_->simulate(c.initial, c.start.line, c.start.col, "}xbwi ff<Esc>");
      auto alt3 = oracle_->simulate(c.initial, c.start.line, c.start.col, "}xciwff<Esc>");
      cerr << "  alt '}xcwff<Esc>': " << alt1.lines << endl;
      cerr << "  alt '}xbwi ff<Esc>': " << alt2.lines << endl;
      cerr << "  alt '}xciwff<Esc>': " << alt3.lines << endl;
      dump("alt1 ", alt1.lines, alt1.row, alt1.col, alt1.mode);
      dump("alt2 ", alt2.lines, alt2.row, alt2.col, alt2.mode);
      dump("alt3 ", alt3.lines, alt3.row, alt3.col, alt3.mode);

      auto replayLikeComposition = [&](const string& seq) {
        Lines simLines = c.initial;
        CursorPos simPos = c.start;
        Mode simMode = Mode::Normal;
        string lastEdit;
        auto tokens = parseSequence(seq);
        for (const auto& tok : tokens) {
          if (tok.type == TokenType::Motion) {
            if (simMode == Mode::Insert) {
              VimCore::insertText(simLines, simPos, tok.text);
            } else {
              simPos = simulateMotions(simPos, tok.text, simLines);
            }
          } else if (tok.type == TokenType::TypedText) {
            if (simMode == Mode::Insert) VimCore::insertText(simLines, simPos, tok.text);
          } else {
            auto edits = Edit::parseEdits(tok.text);
            for (const auto& e : edits) {
              Edit::applyEdit(simLines, simPos, simMode, e, &lastEdit);
            }
          }
        }
        cerr << "  replay '" << seq << "': " << simLines
             << " pos=(" << simPos.line << "," << simPos.col << ") mode="
             << (simMode == Mode::Normal ? "N" : "I") << endl;
      };
      replayLikeComposition("}xcwff<Esc>");
      replayLikeComposition("}xdwi ff<Esc>");

      cerr << "  step trace for alt '}xcwff<Esc>'" << endl;
      {
        const string seqAlt = "}xcwff<Esc>";
        Lines interpAlt = c.initial;
        CursorPos posAlt = c.start;
        Mode modeAlt = Mode::Normal;
        string lastAlt;
        auto editsAlt = Edit::parseEdits(seqAlt);
        string prefixAlt;
        for (int si = 0; si < static_cast<int>(editsAlt.size()); si++) {
          prefixAlt += string(editsAlt[si].edit);
          Edit::applyEdit(interpAlt, posAlt, modeAlt, editsAlt[si], &lastAlt);
          auto nStep = oracle_->simulate(c.initial, c.start.line, c.start.col, prefixAlt);
          bool same = (interpAlt == nStep.lines) &&
                      (posAlt.line == nStep.row) &&
                      (posAlt.col == nStep.col) &&
                      (modeAlt == nStep.mode);
          cerr << "    [" << si << "] '" << editsAlt[si].edit << "'" << (same ? " OK" : " MISMATCH") << endl;
          if (!same) {
            dump("interp", interpAlt, posAlt.line, posAlt.col, modeAlt);
            dump("nvim  ", nStep.lines, nStep.row, nStep.col, nStep.mode);
          }
        }
      }

      auto diffs = Myers::calculate(c.initial, c.goal);
      cerr << "  diffs: " << diffs.size() << endl;
      Config cfg = Config::uniform();
      EditOptimizer eopt(cfg);
      for (size_t di = 0; di < diffs.size(); di++) {
        const auto& d = diffs[di];
        cerr << "    diff[" << di << "] del='" << d.deletedText
             << "' ins='" << d.insertedText
             << "' begin=" << d.beginPos << " end=" << d.endPos
             << " pre='" << d.boundary.prefix() << "' suf='" << d.boundary.suffix() << "'" << endl;
        if (d.isPureInsertion()) continue;
        EditResult er = eopt.optimizeEdit(
            d.deletedLines(), d.insertedLines(), d.boundary,
            EditOptimizerParams{}.withMaxResults(INT_MAX),
            d.beginPos.line, d.beginPos.col, d.beginPos);
        for (size_t ri = 0; ri < er.resultCount(); ri++) {
          const auto& r = er.getResults()[ri];
          if (!r.isValid()) continue;
          int fullCol = static_cast<int>(ri) + (d.beginPos.line == 0 ? d.beginPos.col : 0);
          auto n = oracle_->simulate(c.initial, d.beginPos.line, fullCol, r.sequence.str());
          cerr << "      pos[" << ri << "] seq='" << r.sequence
               << "' nvim=" << n.lines << endl;
        }
      }
    }

    if (ci == 0) {
      Lines interp = c.initial;
      CursorPos pos = c.start;
      Mode mode = Mode::Normal;
      string lastEdit;
      auto edits = Edit::parseEdits(c.seq);
      string prefix;
      for (int i = 0; i < static_cast<int>(edits.size()); i++) {
        const ParsedEdit& op = edits[i];
        prefix += string(op.edit);
        Edit::applyEdit(interp, pos, mode, op, &lastEdit);
        auto nvimStep = oracle_->simulate(c.initial, c.start.line, c.start.col, prefix);
        bool same = (interp == nvimStep.lines) &&
                    (pos.line == nvimStep.row) &&
                    (pos.col == nvimStep.col) &&
                    (mode == nvimStep.mode);
        cerr << " step[" << i << "] '" << op.edit << "'" << (same ? " OK" : " MISMATCH") << endl;
        if (!same) {
          dump("interp", interp, pos.line, pos.col, mode);
          dump("nvim  ", nvimStep.lines, nvimStep.row, nvimStep.col, nvimStep.mode);
        }
      }
    }
  }
}
