// tests/Debug/Debug.cpp
//
// Debug utilities and scratch tests for development.
// Enable a test by removing DISABLED_ prefix.
//
// Run: ./build/tests/vimficiency_debug --gtest_filter="DebugTest.*"
//   - Or: ./vimficiency_tests --gtest_filter="NeovimOracleDebug.*"

#include <gtest/gtest.h>
#include <queue>

#include "Interpreter/EditInterpreter.h"
#include "Keyboard/Config.h"
#include "Optimizer/TransformOptimizer/TransformOptimizer.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Optimizer/CompositionOptimizer/CompositionSearchContext.h"
#include "Optimizer/CompositionOptimizer/DiffState.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Optimizer/NavOptimizer/NavRangeConversion.h"
#include "Boundary/TransformBoundary.h"
#include "Boundary/NavBoundary.h"
#include "Utils/EditTestGenerators.h"
#include "Utils/NeovimOracle.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/StringUtils.h"
#include "Optimizer/TransformOptimizer/TransformState.h"
#include "VimCore/VimEditUtils.h"
#include "VimCore/VimEndpointUtils.h"

using namespace std;

namespace {
TransformResult pureDeletionResult(
    TransformOptimizer& opt,
    const Lines& initialLines,
    TransformBoundary boundary,
    TransformOptimizerParams params = {}) {
  return opt.optimizePureDeletion(initialLines, boundary, params);
}

template<bool TrackExploredStates>
[[gnu::noinline]] int searchStatsHotLoop(int iterations) {
  NavSearchStats stats;
  for (int i = 0; i < iterations; i++) {
    stats.incrementNodesExplored();
    stats.incrementMotionsEmitted();
    stats.incrementStatesSkipped();
    if constexpr (TrackExploredStates) {
      stats.maybeRecordExploredState(i, i, static_cast<double>(i), "x");
    }
  }
  return stats.nodesExplored() + stats.motionsEmitted()
      + stats.statesSkipped() + static_cast<int>(stats.exploredStates().size());
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
  TransformOptimizerParams params = TransformOptimizerParams{}.withMaxNodesPopped(100000);

  TransformOptimizer makeOptimizer() {
    return TransformOptimizer(config);
  }

  // Create boundary for full buffer deletion (no constraints)
  TransformBoundary makeFullBufferBoundary(const Lines& source) {
    return TransformBoundary(source, CursorPos(0, 0), source.endPos());
  }
};

TEST_F(DebugTest, DISABLED_SearchStatsCodegen) {
  cerr << "NoTrace=" << searchStatsHotLoop<false>(1000) << endl;
  cerr << "WithTrace=" << searchStatsHotLoop<true>(1000) << endl;
}

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
  TransformBoundary boundary(buffer, editBegin, editEnd);

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
  TransformOptimizer opt(config);
  int mr = max(10, editRegion.totalPositions() / 4);
  TransformOptimizerParams p = TransformOptimizerParams{}.withMaxResults(mr);

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

  // Detailed trace of the crashing sequence
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
    TransformOptimizer opt2(config);
    auto result = pureDeletionResult(opt2, editRegion, boundary, p);
    cerr << "Pure deletion OK, results=" << result.resultCount() << endl;

    // Now replay each result
    for (size_t i = 0; i < result.resultCount(); i++) {
      const auto& bucket = result.getResults()[i];
      if (bucket.empty()) continue;
      const auto& r = bucket[0];
      const string& seq = r.getSequence().str();

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
    auto result = opt.optimizeTransform(editRegion, goal, boundary, p);
    cerr << "Full optimizer OK, results=" << result.resultCount() << endl;
  } catch (const exception& ex) {
    cerr << "OPTIMIZER CRASH: " << ex.what() << endl;
  }

  // Test without counted edits: use minCountRepeat=999 to disable
  cerr << "\n--- Without counted edits (minCountRepeat=999) ---" << endl;
  try {
    TransformOptimizer opt3(config);
    TransformOptimizerParams p3 = TransformOptimizerParams{}.withMaxResults(mr).withMinCountRepeat(999);
    auto result = opt3.optimizeTransform(editRegion, goal, boundary, p3);
    cerr << "No-counted OK, results=" << result.resultCount() << endl;
  } catch (const exception& ex) {
    cerr << "No-counted CRASH: " << ex.what() << endl;
  }
}

TEST_F(DebugTest, DISABLED_InvestigateMissingTypedCharAfterSubstitute) {
  Lines initial = {
      "int main() {",
      "  int m;",
      "  for(int i = 2; i < n; i++) {",
      "",
      "  }",
      "}",
  };
  Lines goal = {
      "int main() {",
      "  int m;",
      "  for(int i = 3; i < n; i++) {",
      "",
      "  }",
      "}",
  };

  CursorPos initialPos(1, 2);  // 'i' in "  int m;"
  CursorPos goalPos(2, 14);    // '3' in "  for(int i = 3; ..."
  string userSeq = string("jf;i") + "\x08" + "3" + "\x1b";

  cerr << "\n=== InvestigateMissingTypedCharAfterSubstitute ===" << endl;
  cerr << "Initial: " << initial << endl;
  cerr << "Goal:    " << goal << endl;
  cerr << "Initial pos: " << initialPos << " goalPos: " << goalPos << endl;
  SequenceTracer::printSequenceBytes(userSeq);

  vector<DiffState> diffs = Myers::calculate(initial, goal);
  cerr << "\n=== Diffs ===" << endl;
  for (size_t i = 0; i < diffs.size(); i++) {
    const auto& d = diffs[i];
    cerr << "diff[" << i << "] begin=" << d.beginPos
         << " end=" << d.endPos
         << " del='" << makePrintable(d.deletedText) << "'"
         << " ins='" << makePrintable(d.insertedText) << "'"
         << " prefix='" << makePrintable(d.boundary.prefix()) << "'"
         << " suffix='" << makePrintable(d.boundary.suffix()) << "'"
         << endl;
  }

  ASSERT_EQ(diffs.size(), 1u);
  const auto& diff = diffs[0];

  TransformOptimizer editOpt(config);
  TransformOptimizerParams editParams{};
  editParams.withMaxResults(20).withMaxNodesPopped(10000).withMaxResultsPerStartPos(5);
  auto transformResult = editOpt.optimizeTransform(
      diff.deletedLines(), diff.insertedLines(), diff.boundary, editParams,
      diff.beginPos.line, diff.beginPos.col, goalPos);

  cerr << "\n=== TransformResult At Diff Begin ===" << endl;
  cerr << "edit goalPos=" << transformResult.getGoalPos() << endl;
  auto localResults = transformResult.resultsAt(diff.beginPos.line, diff.beginPos.col);
  auto oracle = make_unique<NeovimOracle>();
  for (size_t i = 0; i < localResults.size(); i++) {
    const string& rawSeq = localResults[i].getSequence().str();
    cerr << "  [" << i << "] seq='" << localResults[i].getSequence()
         << "' raw='" << makePrintable(rawSeq) << "'"
         << " cost=" << localResults[i].getCost()
         << " bytes=";
    for (char c : rawSeq) {
      cerr << static_cast<int>(static_cast<unsigned char>(c)) << " ";
    }
    auto nvim = oracle->simulate(initial, initialPos.line, initialPos.col,
                                 string("$Ef2") + rawSeq);
    bool ok = (nvim.lines == goal && nvim.row == goalPos.line && nvim.col == goalPos.col);
    cerr << " oracleFrom'$Ef2'=" << (ok ? "OK" : "WRONG")
         << " finalLines=" << nvim.lines
         << " finalPos=(" << nvim.row << "," << nvim.col << ")"
         << endl;
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

  // Trace the sequence in Neovim.
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
    TransformOptimizer opt(config);
    TransformBoundary boundary(fullBuffer, CursorPos(0, 1), CursorPos(3, 3));  // prefix="a", suffix="bd"
    TransformResult res = pureDeletionResult(opt, editRegion, boundary, params);
    int idx = 0;
    for (int line = 0; line < static_cast<int>(editRegion.size()); line++) {
      int cols = editRegion[line].empty() ? 1 : static_cast<int>(editRegion[line].size());
      for (int col = 0; col < cols; col++) {
        const auto& bucket = res.getResults()[idx];
        if (!bucket.empty() && idx == 4) {  // editPos [0,4]
          const auto& r = bucket[0];
          cerr << "editPos=[" << line << "," << col << "] seq='" << r.getSequence() << "' cost=" << r.getCost() << endl;
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

  // Case 4: Sub-line prefix match (interesting for TransformOptimizer trimming)
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
    const auto& results = compResult.getResults();

    cerr << "Results: " << results.size() << endl;
    for (size_t i = 0; i < results.size(); i++) {
      const auto& seq = results[i].getSequence();
      // Print sequence bytes
      cerr << "  [" << i << "] seq='" << seq << "' (len=" << seq.size() << ")" << endl;
      cerr << "       bytes: ";
      for (char c : seq.view()) cerr << static_cast<int>(static_cast<unsigned char>(c)) << " ";
      cerr << endl;
      cerr << "       cost=" << results[i].getCost() << endl;

      auto nvim = oracle->simulate(initial, 0, 0, seq.str());
      cerr << "       nvim: " << nvim.lines << (nvim.lines == goal ? " OK" : " WRONG") << endl;
    }
  }

  // Also trace what the transform optimizer produces for each diff.
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

    // Transform optimizer for each diff
    Config config = Config::uniform();
    TransformOptimizer editOpt(config);
    for (size_t i = 0; i < diffs.size(); i++) {
      const auto& d = diffs[i];
      if (d.isPureInsertion()) continue;
      TransformResult result = editOpt.optimizeTransform(
          d.deletedLines(), d.insertedLines(), d.boundary, {},
          d.beginPos.line, d.beginPos.col, d.beginPos);
      cerr << "  Edit[" << i << "] goalPos=(" << result.getGoalPos().line << "," << result.getGoalPos().col
           << ") results:" << endl;
      for (size_t j = 0; j < result.resultCount(); j++) {
        const auto& bucket = result.getResults()[j];
        if (!bucket.empty()) {
          cerr << "    pos " << j << ": '" << bucket[0].getSequence()
               << "' cost=" << bucket[0].getCost() << endl;
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
    cerr << "Results: " << compResult.getResults().size() << endl;
    for (size_t i = 0; i < compResult.getResults().size(); i++) {
      cerr << "  [" << i << "] '" << compResult.getResults()[i].getSequence()
           << "' cost=" << compResult.getResults()[i].getCost() << endl;
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
        TransformOptimizer editOpt(config);
        TransformResult result = editOpt.optimizeTransform(
            d.deletedLines(), d.insertedLines(), d.boundary, {},
            d.beginPos.line, d.beginPos.col, d.beginPos);
        int validCount = 0;
        for (size_t j = 0; j < result.resultCount(); j++) {
          if (!result.getResults()[j].empty()) validCount++;
        }
        cerr << "    Edit valid: " << validCount << "/" << result.resultCount()
             << " nodes=" << result.getStats().nodesExplored() << endl;
        for (size_t j = 0; j < result.resultCount(); j++) {
          const auto& bucket = result.getResults()[j];
          if (!bucket.empty()) {
            cerr << "    pos " << j << ": '" << bucket[0].getSequence()
                 << "' cost=" << bucket[0].getCost() << endl;
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

TEST_F(NeovimOracleDebug, DISABLED_InvestigateDotDbBug) {
  // FAIL iter=11 seq='db..s fba<Esc>' initialPos=(0,2)
  //   Initial: 'bbcffdbeafcbfabbdc'
  //   Edit:     [0,5) 'bbcff' -> 'fba'
  //   Goal:    'fbadbeafcbfabbdc'
  //   Got:     fbaffdbeafcbfabbdc

  Lines initial = {"bbcffdbeafcbfabbdc"};
  Lines goal = {"fbadbeafcbfabbdc"};
  CursorPos initialPos(0, 2);

  // Trace the winning sequence with the oracle.
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
    for (size_t i = 0; i < compResult.getResults().size(); i++) {
      const auto& seq = compResult.getResults()[i].getSequence();
      cerr << "  [" << i << "] '" << compResult.getResults()[i].getSequence()
           << "' cost=" << compResult.getResults()[i].getCost() << endl;
      auto nvim = oracle_->simulate(initial, 0, 2, seq.str());
      cerr << "    nvim: " << nvim.lines << (nvim.lines == goal ? " OK" : " WRONG") << endl;
    }
  }

  // Step 3: Run the transform optimizer directly on the diff
  cerr << "\n=== Transform Optimizer for [0,5) ===" << endl;
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
        TransformOptimizer editOpt(config);
        TransformOptimizerParams params = TransformOptimizerParams{}.withMaxResults(INT_MAX);
        TransformResult result = editOpt.optimizeTransform(
            d.deletedLines(), d.insertedLines(), d.boundary, params,
            d.beginPos.line, d.beginPos.col, d.beginPos);
        for (size_t j = 0; j < result.resultCount(); j++) {
          const auto& bucket = result.getResults()[j];
          if (!bucket.empty()) {
            const auto& r = bucket[0];
            cerr << "    pos " << j << ": '" << r.getSequence()
                 << "' cost=" << r.getCost() << endl;
            // Verify each with oracle
            int fullCol = static_cast<int>(j) + (d.beginPos.line == 0 ? d.beginPos.col : 0);
            auto nvim = oracle_->simulate(initial, 0, fullCol, r.getSequence().str());
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
        CharRange range;
        if (col > 0) {
          range = CharRange(simEndpoint, CursorPos(0, col - 1));
        } else {
          cerr << " (col=0, exclusive skip)" << endl;
          continue;
        }
        Lines afterBuf = buf;
        CursorPos afterPos = pos;
        VimCore::deleteRangeAndUpdatePos(afterBuf, range, afterPos);
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
  cerr << "=== Transform optimizer for JoinLinesWithResidual ===" << endl;

  Lines fullBuffer = {"aaa", "xxx", "ccc"};
  // Edit region: the content to change is "\nxxx\nccc" starting at (0,3)
  // initialLines = ["", "xxx", "ccc"] (3 lines - the newline creates empty first line)
  Lines editRegion = {"", "xxx", "ccc"};
  Lines goalLines = {" bbb ccc"};

  CursorPos initialPos(0, 3);  // start of edit region in full buffer
  CursorPos endPos = fullBuffer.endPos();  // end of buffer
  TransformBoundary boundary(fullBuffer, initialPos, endPos);

  cerr << "  prefix='" << boundary.prefix() << "' suffix='" << boundary.suffix() << "'" << endl;
  cerr << "  editRegion=" << editRegion << " goalLines=" << goalLines << endl;

  Config config = Config::uniform();
  TransformOptimizer opt(config);
  TransformOptimizerParams params = TransformOptimizerParams{}.withMaxResults(INT_MAX);
  TransformResult res = opt.optimizeTransform(editRegion, goalLines, boundary, params);

  int idx = 0;
  for (int r = 0; r < static_cast<int>(editRegion.size()); r++) {
    for (int c = 0; c < editRegion[r].effectiveSize(); c++) {
      const auto& bucket = res.getResults()[idx];
      if (!bucket.empty()) {
        const auto& result = bucket[0];
        cerr << "  [" << r << "," << c << "] seq='" << result.getSequence()
             << "' cost=" << result.getCost() << endl;

        // Byte dump for debugging
        cerr << "    bytes:";
        for (unsigned char ch : result.getSequence().view()) {
          if (ch >= 0x20 && ch < 0x7f) cerr << " '" << ch << "'";
          else cerr << " 0x" << std::hex << (int)ch << std::dec;
        }
        cerr << endl;

        // Verify with oracle: apply in the full buffer context
        int fullRow = r + static_cast<int>(initialPos.line);
        int fullCol = c + (r == 0 ? static_cast<int>(initialPos.col) : 0);
        auto nvim = oracle_->simulate(fullBuffer, fullRow, fullCol, result.getSequence().str());
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
    const auto& results = compResult.getResults();

    cerr << "Results: " << results.size() << endl;
    for (size_t i = 0; i < results.size(); i++) {
      const auto& seq = results[i].getSequence();
      cerr << "  " << i << ": '" << seq << "' cost=" << results[i].getCost() << endl;

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
        initial, CursorPos(0, 0), goal, CursorPos(0, 0), "",
        NavContext(), NavBoundary(), params, config);
  };

  // Bug 1: Bracket mask doesn't mark positions INSIDE the brackets
  cerr << "\n=== Bug 1: Bracket positions inside pair ===" << endl;
  {
    Lines initial = {"foo (hello) bar"};
    Lines goal = {"foo (X) bar"};
    auto ctx = makeCtx(initial, goal);
    const auto& toCtx = ctx.edits[0].bracketQuoteContext;
    const auto& diff = ctx.edits[0].diffState;

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
    const auto& toCtx = ctx.edits[0].bracketQuoteContext;
    const auto& diff = ctx.edits[0].diffState;

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
    const auto& toCtx = ctx.edits[0].bracketQuoteContext;
    const auto& diff = ctx.edits[0].diffState;

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

  // Test NavOptimizer.optimize directly
  cerr << endl << "=== Testing NavOptimizer.optimize ===" << endl;
  {
    Config cfg = Config::uniform();
    NavOptimizer movOpt(cfg);
    CursorPos rangeBegin(0, 6);
    CursorPos rangeEnd(0, 11);

    cerr << "Finding path from " << initialPos << " to range [" << rangeBegin << ", " << rangeEnd << ")" << endl;

    auto rangeResult = movOpt.optimize(
        initial, initialPos,
        toMotionInterval(initial, CharRange(rangeBegin, rangeEnd)),
        NavOptimizerParams{}.withMaxResults(10));

    cerr << "NavOptimizer returned " << rangeResult.getResults().size() << " results" << endl;
    cerr << "Stats: nodes=" << rangeResult.getStats().nodesExplored()
         << " stopReason=" << static_cast<int>(rangeResult.getStats().stopReason()) << endl;

    for (size_t i = 0; i < rangeResult.getResults().size() && i < 5; i++) {
      const auto& r = rangeResult.getResults()[i];
      cerr << "  Motion " << i << ": '" << r.getSequence() << "' -> " << r.getGoalPos()
           << " cost=" << r.getCost() << endl;
    }
  }

  // Now run the full optimizer
  Config config = Config::uniform();
  CompositionOptimizer opt{config};
  CompositionOptimizerParams params{};

  cerr << endl << "Running CompositionOptimizer..." << endl;
  auto compResult = opt.optimize(
      initial, initialPos, goal, goalPos, params);
  const auto& results = compResult.getResults();

  cerr << "Results: " << results.size() << endl;
  for (size_t i = 0; i < results.size(); i++) {
    cerr << "  Result " << i << ": '" << results[i].getSequence() << "' cost=" << results[i].getCost() << endl;
  }

  if (!results.empty()) {
    const auto& seq = results[0].getSequence();
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
    CharRange iwRange = VimCore::textObject(pos, ourLines, /*isInner=*/true, /*isBigWord=*/false);
    cerr << "  textObject(iw) range: [(" << iwRange.begin.line << "," << iwRange.begin.col
         << "), (" << iwRange.end.line << "," << iwRange.end.col << ")]" << endl;
    cerr << "  Deleted text: '";
    for (int c = iwRange.begin.col; c <= iwRange.end.col; c++) {
      cerr << ourLines[iwRange.begin.line][c];
    }
    cerr << "'" << endl;

    // Apply deletion (change mode)
    VimCore::deleteRangeAndUpdatePos(ourLines, iwRange, pos, Mode::Insert);
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
    const auto& actualResults = compResult.getResults();
    cerr << "  Total results: " << actualResults.size() << endl;
    auto oracle = make_unique<NeovimOracle>();
    for (size_t i = 0; i < actualResults.size(); i++) {
      auto nvim = oracle->simulate(initial, initialPos.line, initialPos.col, actualResults[i].getSequence().str());
      cerr << "  [" << i << "] seq='" << actualResults[i].getSequence()
           << "' cost=" << actualResults[i].getCost()
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

  cerr << "\n========== STEP 2: TransformOptimizer for each diff ==========" << endl;
  TransformOptimizer editOpt(config);
  for (size_t i = 0; i < diffs.size(); i++) {
    const auto& d = diffs[i];
    if (d.isPureInsertion()) {
      cerr << "  Diff " << i << ": pure insertion, skipping TransformOptimizer" << endl;
      continue;
    }
    TransformResult transformResult = editOpt.optimizeTransform(
        d.deletedLines(), d.insertedLines(), d.boundary, {},
        d.beginPos.line, d.beginPos.col, d.beginPos);

    cerr << "  Diff " << i << ": TransformResult has " << transformResult.resultCount() << " positions" << endl;

    for (size_t j = 0; j < transformResult.resultCount(); j++) {
      const auto& bucket = transformResult.getResults()[j];
      if (!bucket.empty()) {
        const auto& r = bucket[0];
        cerr << "    pos " << j << ": seq='" << r.getSequence() << "' cost=" << r.getCost() << endl;
      } else {
        cerr << "    pos " << j << ": INVALID" << endl;
      }
    }

    // Test resultAt for various cursor positions
    cerr << "  resultAt tests:" << endl;
    for (int col = 0; col < static_cast<int>(initial[0].size()); col++) {
      const Result* r = transformResult.resultAt(0, col);
      if (r) {
        cerr << "    col=" << col << " -> valid result" << endl;
      }
    }
  }

  cerr << "\n========== STEP 3: NavOptimizer optimize ==========" << endl;
  {
    assert(!diffs.empty());
    const auto& d = diffs[0];
    CursorPos rangeBegin = d.beginPos;
    CursorPos rangeEnd = d.endPos;

    cerr << "  CharRange: [(" << rangeBegin.line << "," << rangeBegin.col << "), ("
         << rangeEnd.line << "," << rangeEnd.col << "))" << endl;
    cerr << "  StartPos: (" << initialPos.line << "," << initialPos.col << ")" << endl;

    NavBoundary boundary(initial,
        CursorPos(0, 0),
        CursorPos(0, static_cast<int>(initial[0].size())),
        false, false);

    NavOptimizer navOpt(config);
    NavContext navCtx;

    auto rangeResult = navOpt.optimize(
        initial, initialPos,
        toMotionInterval(initial, CharRange(rangeBegin, rangeEnd)),
        NavOptimizerParams{}.withMaxResults(10), "",
        boundary, navCtx);

    cerr << "  CharRange results: " << rangeResult.getResults().size() << endl;
    for (size_t i = 0; i < rangeResult.getResults().size(); i++) {
      const auto& r = rangeResult.getResults()[i];
      if (!r.getSequence().empty()) {
        cerr << "    [" << i << "] seq='" << r.getSequence() << "' cost=" << r.getCost()
             << " goalPos=(" << r.getGoalPos().line << "," << r.getGoalPos().col << ")" << endl;
      }
    }
  }

  cerr << "\n========== STEP 4: Trace A* Search ==========" << endl;
  {
    CompositionOptimizerParams params{};
    NavOptimizer navOpt(config);
    NavContext navCtx;
    NavBoundary boundary(initial,
        CursorPos(0, 0),
        CursorPos(0, static_cast<int>(initial[0].size()) - 1),
        false, false);

    CompositionSearchContext ctx(initial, initialPos, goal, CursorPos(0, 0), "",
        navCtx, boundary, params, config);

    cerr << "  totalEdits=" << ctx.totalEdits() << endl;
    for (int i = 0; i < ctx.totalEdits(); i++) {
      const auto& d = ctx.edits[i].diffState;
      cerr << "  diff[" << i << "]: begin=(" << d.beginPos.line << "," << d.beginPos.col
           << ") end=(" << d.endPos.line << "," << d.endPos.col << ")" << endl;
      const auto& er = ctx.edits[i].transformResult;
      cerr << "    transformResult: " << er.resultCount() << " positions, goalPos=("
           << er.getGoalPos().line << "," << er.getGoalPos().col << ")" << endl;
    }

    // Seed initial state (same as CompositionOptimizer::optimize does).
    // The priority queue moved out of CompositionSearchContext into the
    // optimizer's local scope; this debug trace mirrors that with a local pq.
    std::priority_queue<CompositionState, std::vector<CompositionState>,
                        std::greater<CompositionState>> pq;
    CompositionState startingState(initialPos, Mode::Normal, 0);
    startingState.setCost(ctx.heuristic(startingState, 0));
    pq.push(startingState);
    ctx.costMap[startingState.getKey()] = startingState.getCost();

    auto enqueueState = [&](CompositionState&& newState) {
      if (newState.getEffort() > ctx.maxEffort) return;

      double newCost = newState.getCost();
      const CompositionStateKey newKey = newState.getKey();
      auto it = ctx.costMap.find(newKey);

      if (it == ctx.costMap.end()) {
        if (newState.getEditsCompleted() != ctx.totalEdits()) {
          ctx.costMap.emplace(newKey, newCost);
        }
        pq.push(std::move(newState));
      } else if (newCost <= it->second) {
        it->second = newCost;
        pq.push(std::move(newState));
      }
    };
    auto enqueueEditTransition = [&](const CompositionState& current,
                                     const Sequence& editSequence,
                                     const CursorPos& goalPos,
                                     int editsAfter) {
      CompositionState newState = current.afterEditTransition(
          editSequence, goalPos, Mode::Normal, config);
      newState.setCost(ctx.heuristic(newState, editsAfter));
      enqueueState(std::move(newState));
    };
    auto enqueueMotionTransition = [&](const CompositionState& current,
                                       const Sequence& moveSequence,
                                       const CursorPos& goalPos,
                                       int editsCompleted) {
      CompositionState newState = current.afterNavResult(
          moveSequence, goalPos, config);
      newState.setCost(ctx.heuristic(newState, editsCompleted));
      enqueueState(std::move(newState));
    };

    // Manual A* trace — pop states and print what happens
    int popCount = 0;
    vector<Result> results;
    while (!pq.empty() && ctx.totalPops < params.maxNodesPopped && popCount < 50) {
      CompositionState s = pq.top();
      pq.pop();
      ctx.totalPops++;
      CursorPos pos = s.getPos();
      int editsCompleted = s.getEditsCompleted();
      popCount++;

      if (editsCompleted == ctx.totalEdits()) {
        cerr << "  POP " << popCount << ": GOAL pos=(" << pos.line << "," << pos.col
             << ") edits=" << editsCompleted
             << " seq='" << s.getSequence() << "' effort=" << s.getEffort()
             << " cost=" << s.getCost() << endl;
        results.emplace_back(s.getSequence().str(), s.getRunningEffort().getEffort(config));
        if (results.size() >= 3) break;
        continue;
      }

      auto it = ctx.costMap.find(s.getKey());
      if (it != ctx.costMap.end() && it->second < s.getCost()) {
        cerr << "  POP " << popCount << ": STALE pos=(" << pos.line << "," << pos.col
             << ") edits=" << editsCompleted << " seq='" << s.getSequence() << "'" << endl;
        continue;
      }
      ctx.nodesProcessed++;

      const Lines& currentLines = ctx.getLinesAfter(editsCompleted);
      const DiffState& nextEdit = ctx.getDiffState(editsCompleted);

      if (nextEdit.isPureInsertion()) {
        cerr << "  POP " << popCount << ": PURE_INS pos=(" << pos.line << "," << pos.col
             << ") edits=" << editsCompleted << " seq='" << s.getSequence() << "'" << endl;
        continue; // skip insertion handling for this trace
      }

      const TransformResult& transformResult = ctx.edits[editsCompleted].transformResult;
      const Result* editRes = transformResult.resultAt(pos.line, pos.col);

      cerr << "  POP " << popCount << ": pos=(" << pos.line << "," << pos.col
           << ") edits=" << editsCompleted << " seq='" << s.getSequence()
           << "' effort=" << s.getEffort() << " cost=" << s.getCost()
           << " hasResult=" << (editRes ? "yes" : "no") << endl;

      if (editRes) {
        // Edit transition
        cerr << "    -> EDIT: seq='" << editRes->getSequence() << "'" << endl;
        enqueueEditTransition(s, editRes->getSequence(),
                              transformResult.getGoalPos(), editsCompleted + 1);
      } else {
        // Motion search
        int editEndLine = nextEdit.endPos.line + (nextEdit.endPos.col > 0 ? 1 : 0);
        auto [beginLine, endLine] = currentLines.minmaxBoundWithPadding(
            min(pos.line, nextEdit.beginPos.line),
            max(pos.line + 1, editEndLine),
            params.navPaddingAbove, params.navPaddingBelow);

        Lines subset = currentLines.getLineRange(beginLine, endLine);
        CursorPos localPos(pos.line - beginLine, pos.col, pos.targetCol);
        CursorPos localRangeBegin(nextEdit.beginPos.line - beginLine, nextEdit.beginPos.col);
        CursorPos localRangeEnd(nextEdit.endPos.line - beginLine, nextEdit.endPos.col);

        CursorPos subsetEnd(static_cast<int>(subset.size()) - 1,
            subset.back().effectiveSize());
        NavBoundary subsetBoundary(subset, localRangeBegin, subsetEnd,
            beginLine > 0 || boundary.hasLinesAbove(),
            endLine <= currentLines.lastLine() || boundary.hasLinesBelow());

        auto movementResults = navOpt.optimize(
            subset, localPos,
            toMotionInterval(subset, CharRange(localRangeBegin, localRangeEnd)),
            NavOptimizerParams{}.withMaxResults(
                clamp(nextEdit.origCharCount(), 1, 10)), "",
            subsetBoundary, navCtx).getResults();

        for (const auto& movResult : movementResults) {
          if (movResult.getSequence().empty()) continue;
          CursorPos goalPos(movResult.getGoalPos().line + beginLine, movResult.getGoalPos().col);
          cerr << "    -> MOTION: seq='" << movResult.getSequence() << "' goalPos=("
               << goalPos.line << "," << goalPos.col << ")" << endl;
          enqueueMotionTransition(s, movResult.getSequence(), goalPos, editsCompleted);
        }
      }
    }

    cerr << "\nFinal results: " << results.size() << endl;
    auto oracle = make_unique<NeovimOracle>();
    for (size_t i = 0; i < results.size(); i++) {
      auto nvim = oracle->simulate(initial, initialPos.line, initialPos.col, results[i].getSequence().str());
      cerr << "  [" << i << "] seq='" << results[i].getSequence() << "' cost=" << results[i].getCost()
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
  CompositionSearchContext ctx(initial, initialPos, goal, CursorPos(0, 0), "",
      NavContext(), NavBoundary(), compParams, config);
  cerr << "totalEdits=" << ctx.totalEdits() << endl;
  for (int i = 0; i < ctx.totalEdits(); i++) {
    const auto& d = ctx.edits[i].diffState;
    cerr << "  diff[" << i << "] begin=(" << d.beginPos.line << "," << d.beginPos.col
         << ") end=(" << d.endPos.line << "," << d.endPos.col << ")"
         << " del='" << makePrintable(d.deletedText) << "'"
         << " ins='" << makePrintable(d.insertedText) << "'"
         << " type=" << (d.isPureInsertion() ? "INSERT" : d.isPureDeletion() ? "DELETE" : "REPLACE")
         << endl;
    cerr << "    buffer[" << i << "]: " << ctx.getLinesAfter(i) << endl;
  }

  // Step 2: Edit results for each diff
  cerr << "\n=== Step 2: EditResults per diff ===" << endl;
  for (int i = 0; i < ctx.totalEdits(); i++) {
    const auto& er = ctx.edits[i].transformResult;
    const auto& d = ctx.edits[i].diffState;
    cerr << "  edit[" << i << "] goalPos=(" << er.getGoalPos().line << "," << er.getGoalPos().col
         << ") resultCount=" << er.resultCount() << endl;

    // Show valid results at each position in the edit region
    int validCount = 0;
    for (size_t j = 0; j < er.getResults().size(); j++) {
      const auto& bucket = er.getResults()[j];
      if (!bucket.empty()) {
        validCount++;
        if (validCount <= 5) {
          cerr << "    pos " << j << ": '" << bucket[0].getSequence() << "' cost="
               << bucket[0].getCost() << endl;
        }
      }
    }
    cerr << "    total valid: " << validCount << " / " << er.resultCount() << endl;

    // Specifically check positions that should have results
    const auto& buf = ctx.getLinesAfter(i);
    for (int line = d.beginPos.line; line <= min(d.endPos.line, static_cast<int>(buf.size()) - 1); line++) {
      int startCol = (line == d.beginPos.line) ? d.beginPos.col : 0;
      int endCol = (line == d.endPos.line) ? d.endPos.col : static_cast<int>(buf[line].size());
      for (int col = startCol; col < endCol; col++) {
        const Result* r = er.resultAt(line, col);
        if (r) {
          cerr << "    resultAt(" << line << "," << col << "): '"
               << r->getSequence() << "' cost=" << r->getCost() << endl;
        }
      }
    }
  }

  // Step 3: A* search trace
  cerr << "\n=== Step 3: A* Search Trace ===" << endl;
  NavOptimizer navOpt(config);
  NavContext navCtx;
  NavBoundary boundary;

  // Local pq mirrors the templated optimizer's pq (moved out of context).
  std::priority_queue<CompositionState, std::vector<CompositionState>,
                      std::greater<CompositionState>> pq;
  CompositionState startingState(initialPos, Mode::Normal, 0);
  startingState.setCost(ctx.heuristic(startingState, 0));
  pq.push(startingState);
  ctx.costMap[startingState.getKey()] = startingState.getCost();

  auto enqueueState = [&](CompositionState&& newState) {
    if (newState.getEffort() > ctx.maxEffort) return;

    double newCost = newState.getCost();
    const CompositionStateKey newKey = newState.getKey();
    auto it = ctx.costMap.find(newKey);

    if (it == ctx.costMap.end()) {
      if (newState.getEditsCompleted() != ctx.totalEdits()) {
        ctx.costMap.emplace(newKey, newCost);
      }
      pq.push(std::move(newState));
    } else if (newCost <= it->second) {
      it->second = newCost;
      pq.push(std::move(newState));
    }
  };
  auto enqueueEditTransition = [&](const CompositionState& current,
                                   const Sequence& editSequence,
                                   const CursorPos& goalPos,
                                   int editsAfter) {
    CompositionState newState = current.afterEditTransition(
        editSequence, goalPos, Mode::Normal, config);
    newState.setCost(ctx.heuristic(newState, editsAfter));
    enqueueState(std::move(newState));
  };
  auto enqueueMotionTransition = [&](const CompositionState& current,
                                     const Sequence& moveSequence,
                                     const CursorPos& goalPos,
                                     int editsCompleted) {
    CompositionState newState = current.afterNavResult(
        moveSequence, goalPos, config);
    newState.setCost(ctx.heuristic(newState, editsCompleted));
    enqueueState(std::move(newState));
  };

  int popCount = 0;
  vector<Result> results;
  while (!pq.empty() && ctx.totalPops < compParams.maxNodesPopped && popCount < 100) {
    CompositionState s = pq.top();
    pq.pop();
    ctx.totalPops++;
    CursorPos pos = s.getPos();
    int editsCompleted = s.getEditsCompleted();
    popCount++;

    if (editsCompleted == ctx.totalEdits()) {
      cerr << "  POP " << popCount << ": GOAL seq='" << s.getSequence()
           << "' effort=" << s.getEffort() << " cost=" << s.getCost() << endl;
      results.emplace_back(s.getSequence().str(), s.getRunningEffort().getEffort(config));
      if (results.size() >= 5) break;
      continue;
    }

    auto it = ctx.costMap.find(s.getKey());
    if (it != ctx.costMap.end() && it->second < s.getCost()) {
      cerr << "  POP " << popCount << ": STALE pos=(" << pos.line << "," << pos.col
           << ") edits=" << editsCompleted << " seq='" << s.getSequence() << "'" << endl;
      continue;
    }
    ctx.nodesProcessed++;

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
    const TransformResult& transformResult = ctx.edits[editsCompleted].transformResult;
    const Result* res = transformResult.resultAt(pos.line, pos.col);

    if (res) {
      cerr << "    -> EDIT: '" << res->getSequence() << "' cost=" << res->getCost()
           << " -> goalPos=(" << transformResult.getGoalPos().line << "," << transformResult.getGoalPos().col << ")" << endl;
      enqueueEditTransition(s, res->getSequence(), transformResult.getGoalPos(), editsCompleted + 1);
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
          compParams.navPaddingAbove, compParams.navPaddingBelow);

      Lines subset = currentLines.getLineRange(beginLine, endLine);
      CursorPos localPos(pos.line - beginLine, pos.col, pos.targetCol);
      CursorPos localRangeBegin(nextEdit.beginPos.line - beginLine, nextEdit.beginPos.col);
      CursorPos localRangeEnd(nextEdit.endPos.line - beginLine, nextEdit.endPos.col);

      CursorPos subsetFirst(0, 0);
      CursorPos subsetEnd(static_cast<int>(subset.size()) - 1,
          subset.back().effectiveSize());
      NavBoundary subsetBoundary(subset, subsetFirst, subsetEnd,
          beginLine > 0, endLine <= currentLines.lastLine());

      auto rangeResults = navOpt.optimize(
          subset, localPos,
          toMotionInterval(subset, CharRange(localRangeBegin, localRangeEnd)),
          NavOptimizerParams{}.withMaxResults(
              clamp(nextEdit.origCharCount(), 1, 10)), "",
          subsetBoundary, navCtx).getResults();

      cerr << "    -> MOTIONS found: " << rangeResults.size() << endl;
      for (const auto& movResult : rangeResults) {
        if (movResult.getSequence().empty()) continue;
        CursorPos goalPos(movResult.getGoalPos().line + beginLine, movResult.getGoalPos().col);
        cerr << "      motion '" << movResult.getSequence() << "' -> ("
             << goalPos.line << "," << goalPos.col << ")" << endl;
        enqueueMotionTransition(s, movResult.getSequence(), goalPos, editsCompleted);
      }
    }
  }

  cerr << "\nSearch exhausted after " << popCount << " pops, " << results.size() << " results" << endl;
  cerr << "Queue remaining: " << static_cast<int>(pq.size()) << endl;

  // Verify results
  if (!results.empty()) {
    auto oracle = make_unique<NeovimOracle>();
    for (size_t i = 0; i < results.size(); i++) {
      auto nvim = oracle->simulate(initial, 0, 0, results[i].getSequence().str());
      cerr << "  [" << i << "] '" << results[i].getSequence() << "' "
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
    CompositionSearchContext ctx(initial, initialPos, goal, CursorPos(0, 0), "",
        NavContext(), NavBoundary(), compParams, config);
    cerr << "totalEdits=" << ctx.totalEdits() << endl;

    for (int i = 0; i < ctx.totalEdits(); i++) {
      const auto& d = ctx.edits[i].diffState;
      cerr << "  diff[" << i << "] begin=(" << d.beginPos.line << "," << d.beginPos.col
           << ") end=(" << d.endPos.line << "," << d.endPos.col << ")"
           << " del='" << makePrintable(d.deletedText) << "'"
           << " ins='" << makePrintable(d.insertedText) << "'" << endl;
      cerr << "    buffer[" << i << "]: " << ctx.getLinesAfter(i);

      if (ctx.edits[i].joinPlan) {
        cerr << "    JOIN PLAN: seq='" << ctx.edits[i].joinPlan->sequence.view()
             << "' effort=" << ctx.edits[i].joinPlan->effort
             << " entryLine=" << ctx.edits[i].joinPlan->entryLine
             << " goalPos=(" << ctx.edits[i].joinPlan->goalPos.line
             << "," << ctx.edits[i].joinPlan->goalPos.col << ")" << endl;
      } else {
        cerr << "    JOIN PLAN: none" << endl;
      }
    }

    // Step 3: Full optimizer
    CompositionOptimizer opt{config};
    auto compResult = opt.optimize(initial, initialPos, goal, goal.lastPos(), compParams);
    cerr << "Results: " << compResult.getResults().size() << endl;
    for (size_t i = 0; i < compResult.getResults().size(); i++) {
      cerr << "  [" << i << "] '" << compResult.getResults()[i].getSequence()
           << "' cost=" << compResult.getResults()[i].getCost() << endl;
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

  // Case 3b: Debug navOptimizer for PartialJoin
  cerr << "\n--- PartialJoin NavOptimizer debug ---" << endl;
  {
    Lines buffer = {"aaa bbb", "ccc", "ddd"};
    CursorPos pos(0, 3);
    CursorPos rangeBegin(1, 3);
    CursorPos rangeEnd(2, 0);
    NavBoundary boundary(buffer, CursorPos(0, 0), buffer.endPos());

    NavOptimizer navOpt(config);
    NavContext navCtx;
    auto rangeResult = navOpt.optimize(
        buffer, pos,
        toMotionInterval(buffer, CharRange(rangeBegin, rangeEnd)),
        NavOptimizerParams{}.withMaxResults(5), "",
        boundary, navCtx);

    cerr << "Motion results: " << rangeResult.getResults().size() << endl;
    for (size_t i = 0; i < rangeResult.getResults().size(); i++) {
      if (!rangeResult.getResults()[i].getSequence().empty()) {
        cerr << "  [" << i << "] '" << rangeResult.getResults()[i].getSequence()
             << "' -> (" << rangeResult.getResults()[i].getGoalPos().line << ","
             << rangeResult.getResults()[i].getGoalPos().col << ")" << endl;
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

  // Step 2: CompositionSearchContext (tests calculateLinesAfterDiffs + calculateTransformResults)
  cerr << "\n=== CompositionSearchContext ===" << endl;
  CompositionOptimizerParams compParams{};
  CompositionSearchContext ctx(initial, initialPos, goal, CursorPos(0, 0), "",
      NavContext(), NavBoundary(), compParams, config);
  cerr << "totalEdits=" << ctx.totalEdits() << endl;
  for (int i = 0; i < ctx.totalEdits(); i++) {
    const auto& d = ctx.edits[i].diffState;
    cerr << "  [" << i << "] begin=(" << d.beginPos.line << "," << d.beginPos.col
         << ") end=(" << d.endPos.line << "," << d.endPos.col << ")"
         << " del='" << makePrintable(d.deletedText) << "'"
         << " ins='" << makePrintable(d.insertedText) << "'"
         << " type=" << (d.isPureInsertion() ? "INSERT" : d.isPureDeletion() ? "DELETE" : "REPLACE")
         << endl;
    cerr << "    buffer[" << i << "]: " << ctx.getLinesAfter(i) << endl;
    cerr << "    boundary: prefix='" << d.boundary.prefix() << "' suffix='" << d.boundary.suffix() << "'"
         << " linesAbove=" << d.boundary.hasLinesAbove()
         << " linesBelow=" << d.boundary.hasLinesBelow() << endl;
  }
  cerr << "  goalBuffer: " << ctx.getLinesAfter(ctx.totalEdits()) << endl;

  // Step 3: Try each edit independently through TransformOptimizer
  cerr << "\n=== TransformOptimizer per diff ===" << endl;
  TransformOptimizer editOpt(config);
  for (int i = 0; i < ctx.totalEdits(); i++) {
    const auto& d = ctx.edits[i].diffState;
    if (d.isPureInsertion()) {
      cerr << "  diff[" << i << "]: pure insertion, skip" << endl;
      continue;
    }
    cerr << "  diff[" << i << "]: calling optimizeTransform..." << endl;
    cerr << "    deletedLines: " << d.deletedLines() << endl;
    cerr << "    insertedLines: " << d.insertedLines() << endl;
    cerr << "    boundary prefix='" << d.boundary.prefix() << "' suffix='" << d.boundary.suffix() << "'" << endl;
    cerr << "    lineBase=" << d.beginPos.line << " colBase=" << d.beginPos.col << endl;

    TransformResult result = editOpt.optimizeTransform(
        d.deletedLines(), d.insertedLines(), d.boundary, {},
        d.beginPos.line, d.beginPos.col, d.beginPos);

    cerr << "    -> results: " << result.getStats().resultsFound()
         << " nodes: " << result.getStats().nodesExplored() << endl;
    for (size_t j = 0; j < result.resultCount(); j++) {
      const auto& bucket = result.getResults()[j];
      if (!bucket.empty()) {
        cerr << "    [" << j << "] '" << bucket[0].getSequence()
             << "' cost=" << bucket[0].getCost() << endl;
      }
    }
  }

  // Step 4: Show what upstream fix would produce (stripped empty first line)
  cerr << "\n=== Upstream fix comparison ===" << endl;
  {
    const auto& d = ctx.edits[0].diffState;
    Lines deleted = d.deletedLines();
    Lines inserted = d.insertedLines();
    cerr << "  Original: deletedLines=" << deleted << " → insertedLines=" << inserted << endl;

    if (deleted.size() > 1 && deleted[0].empty() && !d.boundary.prefix().empty()) {
      deleted.erase(deleted.begin());
      cerr << "  After strip: deletedLines=" << deleted << " → insertedLines=" << inserted << endl;
      cerr << "  Edit region now starts at (1,0), no prefix" << endl;
      cerr << "  Buffer before edit: " << ctx.getLinesAfter(0) << endl;

      // If TransformOptimizer transforms ["bbb","ccc"] → [" bbb ccc?"],
      // what does the buffer look like?
      Lines beforeEdit = ctx.getLinesAfter(0);
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
  CompositionSearchContext ctx(initial, CursorPos(0,0), goal, CursorPos(0, 0), "",
      NavContext(), NavBoundary(), compParams, config);
  cerr << "totalEdits=" << ctx.totalEdits() << endl;
  for (int i = 0; i < ctx.totalEdits(); i++) {
    const auto& d = ctx.edits[i].diffState;
    cerr << "  [" << i << "] begin=(" << d.beginPos.line << "," << d.beginPos.col
         << ") end=(" << d.endPos.line << "," << d.endPos.col << ")"
         << " del='" << d.deletedText << "' ins='" << d.insertedText << "'"
         << " type=" << (d.isPureInsertion() ? "INSERT" : d.isPureDeletion() ? "DELETE" : "REPLACE")
         << endl;
    cerr << "    buffer[" << i << "]: " << ctx.getLinesAfter(i) << endl;
  }

  // Step 3: Full optimizer results with oracle verification
  cerr << "\n=== Optimizer Results ===" << endl;
  CompositionOptimizer opt{config};
  auto compResult = opt.optimize(initial, CursorPos(0,0), goal, CursorPos(0,0), compParams);
  cerr << compResult;

  auto oracle = make_unique<NeovimOracle>();
  for (size_t i = 0; i < compResult.getResults().size(); i++) {
    const auto& seq = compResult.getResults()[i].getSequence();
    auto nvim = oracle->simulate(initial, 0, 0, seq.str());
    bool correct = (nvim.lines == goal);
    cerr << "  [" << i << "] oracle: " << (correct ? "OK" : "WRONG")
         << " got=" << nvim.lines << endl;
  }
}

// =============================================================================
// TransformOptimizer for multi-line diff: why only 1 starting position finds a result
// =============================================================================

TEST_F(DebugTest, SuffixCacheComparison) {
  // Compare standard vs suffix-cached TransformOptimizer on the Switzerland -> Florida case
  Lines deletedLines = {"Switzerland", "Inconspicuous, even"};
  Lines insertedLines = {"Florida"};

  Lines bufferAtEdit = {"I saw a pig in barn in Switzerland", "Inconspicuous, even"};
  CursorPos editBeginPos(0, 23);
  CursorPos editEndPos(1, 19);
  TransformBoundary boundary(bufferAtEdit, editBeginPos, editEndPos);

  TransformOptimizer editOpt(config);

  // Standard search
  cerr << "\n=== Standard optimizeTransform ===" << endl;
  TransformResult stdResult = editOpt.optimizeTransform(
      deletedLines, insertedLines, boundary, params,
      editBeginPos.line, editBeginPos.col, CursorPos(0, 29));

  int stdValid = 0;
  for (size_t i = 0; i < stdResult.resultCount(); i++) {
    if (!stdResult.getResults()[i].empty()) stdValid++;
  }
  cerr << "  nodes=" << stdResult.getStats().nodesExplored()
       << " results=" << stdResult.getStats().resultsFound()
       << " valid=" << stdValid << "/" << stdResult.resultCount()
       << " stop=" << to_string(stdResult.getStats().stopReason()) << endl;
  for (size_t i = 0; i < stdResult.resultCount(); i++) {
    const auto& bucket = stdResult.getResults()[i];
    if (!bucket.empty()) {
      cerr << "  pos " << i << ": '" << bucket[0].getSequence()
           << "' cost=" << bucket[0].getCost() << endl;
    }
  }

  // Suffix-cached search
  cerr << "\n=== optimizeTransform (suffix cached) ===" << endl;
  TransformResult cacheResult = editOpt.optimizeTransform(
      deletedLines, insertedLines, boundary, params,
      editBeginPos.line, editBeginPos.col, CursorPos(0, 29));

  int cacheValid = 0;
  for (size_t i = 0; i < cacheResult.resultCount(); i++) {
    if (!cacheResult.getResults()[i].empty()) cacheValid++;
  }
  cerr << "  nodes=" << cacheResult.getStats().nodesExplored()
       << " results=" << cacheResult.getStats().resultsFound()
       << " valid=" << cacheValid << "/" << cacheResult.resultCount()
       << " stop=" << to_string(cacheResult.getStats().stopReason())
       << " cacheHits=" << cacheResult.getStats().cacheHits()
       << " cacheEntries=" << cacheResult.getStats().cacheEntries()
       << " populations=" << cacheResult.getStats().cachePopulations() << endl;
  for (size_t i = 0; i < cacheResult.resultCount(); i++) {
    const auto& bucket = cacheResult.getResults()[i];
    if (!bucket.empty()) {
      cerr << "  pos " << i << ": '" << bucket[0].getSequence()
           << "' cost=" << bucket[0].getCost() << endl;
    }
  }

  // Summary
  cerr << "\n=== Summary ===" << endl;
  cerr << "Standard: " << stdValid << " valid results, "
       << stdResult.getStats().nodesExplored() << " nodes" << endl;
  cerr << "SuffixCache: " << cacheValid << " valid results, "
       << cacheResult.getStats().nodesExplored() << " nodes, "
       << cacheResult.getStats().cacheHits() << " cache hits" << endl;
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

  TransformBoundary boundary(initial, CursorPos(0, 0), initial.endPos());

  TransformResult result = makeOptimizer().optimizeTransform(
      initial, goal, boundary, params,
      0, 0, CursorPos(0, 0));

  // Verify at least one result is valid
  bool anyValid = false;
  for (size_t i = 0; i < result.resultCount(); i++) {
    const auto& bucket = result.getResults()[i];
    if (!bucket.empty()) {
      anyValid = true;
      const auto& seq = bucket[0].getSequence();
      cerr << "  pos " << i << ": '" << seq << "' cost="
           << bucket[0].getCost() << endl;
    }
  }
  ASSERT_TRUE(anyValid) << "No valid results found";

  // Oracle-verify all results
  auto oracle = make_unique<NeovimOracle>();
  int passed = 0, total = 0;
  for (size_t i = 0; i < result.resultCount(); i++) {
    const auto& bucket = result.getResults()[i];
    if (bucket.empty()) continue;
    const auto& r = bucket[0];
    total++;

    CursorPos editPos = fromFlatIndex(static_cast<int>(i), initial);
    auto nvim = oracle->simulate(initial, editPos.line, editPos.col, r.getSequence().str());
    if (nvim.lines == goal) {
      passed++;
    } else {
      cerr << "FAIL pos=" << i << " seq='" << r.getSequence()
           << "' got=" << nvim.lines << " expected=" << goal << endl;
    }
  }
  EXPECT_EQ(passed, total) << passed << "/" << total << " passed";

  // Multi-line test: two indented lines → single line
  cerr << "\n=== Multi-line indented test ===" << endl;
  Lines initial2 = {"    hello", "        world"};
  Lines goal2 = {"replaced"};
  TransformBoundary boundary2(initial2, CursorPos(0, 0), initial2.endPos());

  TransformResult result2 = makeOptimizer().optimizeTransform(
      initial2, goal2, boundary2, params,
      0, 0, CursorPos(0, 0));

  int passed2 = 0, total2 = 0;
  for (size_t i = 0; i < result2.resultCount(); i++) {
    const auto& bucket = result2.getResults()[i];
    if (bucket.empty()) continue;
    const auto& r = bucket[0];
    total2++;

    CursorPos editPos = fromFlatIndex(static_cast<int>(i), initial2);
    auto nvim = oracle->simulate(initial2, editPos.line, editPos.col, r.getSequence().str());
    if (nvim.lines == goal2) {
      passed2++;
    } else {
      cerr << "FAIL pos=" << i << " seq='" << r.getSequence()
           << "' got=" << nvim.lines << " expected=" << goal2 << endl;
    }
  }
  EXPECT_EQ(passed2, total2) << "Multi-line: " << passed2 << "/" << total2 << " passed";
}

TEST_F(DebugTest, InvestigateTransformOptimizerMultiLineDiff) {
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

  TransformBoundary boundary(bufferAtEdit3, editBeginPos, editEndPos);
  cerr << "\n=== TransformBoundary ===" << endl;
  cerr << "  prefix: '" << boundary.prefix() << "' (" << boundary.prefix().size() << " chars)" << endl;
  cerr << "  suffix: '" << boundary.suffix() << "' (" << boundary.suffix().size() << " chars)" << endl;
  cerr << "  hasLinesAbove: " << boundary.hasLinesAbove() << endl;
  cerr << "  hasLinesBelow: " << boundary.hasLinesBelow() << endl;

  // Run TransformOptimizer with default params
  cerr << "\n=== TransformOptimizer (default params) ===" << endl;
  TransformOptimizer editOpt(config);
  TransformOptimizerParams defaultParams;
  cerr << "  maxNodesPopped=" << defaultParams.maxNodesPopped
       << " maxResults=" << defaultParams.maxResults << endl;

  TransformResult result = editOpt.optimizeTransform(
      deletedLines, insertedLines, boundary, defaultParams,
      editBeginPos.line, editBeginPos.col, CursorPos(0, 29));

  cerr << "  stats: nodes=" << result.getStats().nodesExplored()
       << " results=" << result.getStats().resultsFound()
       << " queueSize=" << result.getStats().queueSizeAtStop()
       << " stopReason=" << static_cast<int>(result.getStats().stopReason())
       << " skipped=" << result.getStats().statesSkipped() << endl;

  int validCount = 0;
  for (size_t i = 0; i < result.resultCount(); i++) {
    const auto& bucket = result.getResults()[i];
    if (!bucket.empty()) {
      validCount++;
      cerr << "  pos " << i << ": '" << bucket[0].getSequence()
           << "' cost=" << bucket[0].getCost() << endl;
    }
  }
  cerr << "  valid: " << validCount << " / " << result.resultCount() << endl;

  // Run with much higher budget
  cerr << "\n=== TransformOptimizer (500k pops) ===" << endl;
  TransformOptimizerParams bigParams = TransformOptimizerParams{}
      .withMaxNodesPopped(500000);

  TransformResult bigResult = editOpt.optimizeTransform(
      deletedLines, insertedLines, boundary, bigParams,
      editBeginPos.line, editBeginPos.col, CursorPos(0, 29));

  cerr << "  stats: nodes=" << bigResult.getStats().nodesExplored()
       << " results=" << bigResult.getStats().resultsFound()
       << " queueSize=" << bigResult.getStats().queueSizeAtStop()
       << " stopReason=" << static_cast<int>(bigResult.getStats().stopReason())
       << " skipped=" << bigResult.getStats().statesSkipped() << endl;

  int bigValidCount = 0;
  for (size_t i = 0; i < bigResult.resultCount(); i++) {
    const auto& bucket = bigResult.getResults()[i];
    if (!bucket.empty()) {
      bigValidCount++;
      cerr << "  pos " << i << ": '" << bucket[0].getSequence()
           << "' cost=" << bucket[0].getCost() << endl;
    }
  }
  cerr << "  valid: " << bigValidCount << " / " << bigResult.resultCount() << endl;

  // Run with Dijkstra mode (no heuristic bias)
  cerr << "\n=== TransformOptimizer (Dijkstra) ===" << endl;
  TransformOptimizerParams dijkstraParams = TransformOptimizerParams::dijkstra(30, 500000);

  TransformResult dijResult = editOpt.optimizeTransform(
      deletedLines, insertedLines, boundary, dijkstraParams,
      editBeginPos.line, editBeginPos.col, CursorPos(0, 29));

  cerr << "  stats: nodes=" << dijResult.getStats().nodesExplored()
       << " results=" << dijResult.getStats().resultsFound()
       << " queueSize=" << dijResult.getStats().queueSizeAtStop()
       << " stopReason=" << static_cast<int>(dijResult.getStats().stopReason())
       << " skipped=" << dijResult.getStats().statesSkipped() << endl;

  int dijValidCount = 0;
  for (size_t i = 0; i < dijResult.resultCount(); i++) {
    const auto& bucket = dijResult.getResults()[i];
    if (!bucket.empty()) {
      dijValidCount++;
      cerr << "  pos " << i << ": '" << bucket[0].getSequence()
           << "' cost=" << bucket[0].getCost() << endl;
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
// what the optimizer's TransformState transitions produce.

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
  // (TransformState::afterDeletion delegates to VimCore::deleteRange)
  Lines buf = {"hello world", "foo bar"};

  // x from (0,5): delete single char (space)
  {
    CursorPos start(0, 5);
    CharRange range(start, start);
    auto [editLines, editPos] = applyViaEdit(buf, start, "x");

    Lines coreLines = buf;
    CursorPos corePos = start;
    VimCore::deleteRangeAndUpdatePos(coreLines, range, corePos, Mode::Normal);
    EXPECT_EQ(editLines, coreLines) << "x lines mismatch";
    EXPECT_EQ(editPos.line, corePos.line) << "x line mismatch";
    EXPECT_EQ(editPos.col, corePos.col) << "x col mismatch";
  }

  // D from (0,5): delete to end of line
  {
    CursorPos start(0, 5);
    CharRange range(start, CursorPos(0, static_cast<int>(buf[0].size()) - 1));
    auto [editLines, editPos] = applyViaEdit(buf, start, "D");

    Lines coreLines = buf;
    CursorPos corePos = start;
    VimCore::deleteRangeAndUpdatePos(coreLines, range, corePos, Mode::Normal);
    EXPECT_EQ(editLines, coreLines) << "D lines mismatch";
    EXPECT_EQ(editPos.line, corePos.line) << "D line mismatch";
    EXPECT_EQ(editPos.col, corePos.col) << "D col mismatch";
  }
}

TEST_F(DebugTest, ReplayVerification_Linewise) {
  // Test dd: Edit::applyEdit vs TransformState::afterLinewiseDeletion
  Lines buf = {"first line", "second line", "third line"};

  // dd from line 0
  {
    CursorPos start(0, 3);
    auto [editLines, editPos] = applyViaEdit(buf, start, "dd");

    TransformState state(buf, start, 0, 0.0);
    TransformState after = state.afterLinewiseDeletion(0);
    EXPECT_EQ(editLines, after.getLines()) << "dd line 0 lines mismatch";
    EXPECT_EQ(editPos.line, after.getPos().line) << "dd line 0 pos.line mismatch";
    EXPECT_EQ(editPos.col, after.getPos().col) << "dd line 0 pos.col mismatch";
    EXPECT_EQ(editPos.targetCol, after.getPos().targetCol) << "dd line 0 targetCol mismatch";
  }

  // dd from line 1
  {
    CursorPos start(1, 5);
    auto [editLines, editPos] = applyViaEdit(buf, start, "dd");

    TransformState state(buf, start, 0, 0.0);
    TransformState after = state.afterLinewiseDeletion(1);
    EXPECT_EQ(editLines, after.getLines()) << "dd line 1 lines mismatch";
    EXPECT_EQ(editPos.line, after.getPos().line) << "dd line 1 pos.line mismatch";
    EXPECT_EQ(editPos.col, after.getPos().col) << "dd line 1 pos.col mismatch";
    EXPECT_EQ(editPos.targetCol, after.getPos().targetCol) << "dd line 1 targetCol mismatch";
  }

  // dd on last line (buffer becomes single line)
  {
    CursorPos start(2, 0);
    auto [editLines, editPos] = applyViaEdit(buf, start, "dd");

    TransformState state(buf, start, 0, 0.0);
    TransformState after = state.afterLinewiseDeletion(2);
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

    TransformState state(buf2, start, 0, 0.0);
    TransformState after = state.afterLinewiseDeletion(1);
    EXPECT_EQ(editLines, after.getLines()) << "dd targetCol lines mismatch";
    EXPECT_EQ(editPos, after.getPos()) << "dd targetCol pos mismatch";
  }
}

TEST_F(DebugTest, ReplayVerification_Join) {
  // Test J/gJ: Edit::applyEdit vs TransformState::afterJoin
  Lines buf = {"hello  ", "  world", "end"};

  // J (add space)
  {
    CursorPos start(0, 2);
    auto [editLines, editPos] = applyViaEdit(buf, start, "J");

    TransformState state(buf, start, 0, 0.0);
    TransformState after = state.afterJoin(true);
    EXPECT_EQ(editLines, after.getLines()) << "J lines mismatch";
    EXPECT_EQ(editPos, after.getPos()) << "J pos mismatch";
  }

  // gJ (no space)
  {
    CursorPos start(0, 2);
    auto [editLines, editPos] = applyViaEdit(buf, start, "gJ");

    TransformState state(buf, start, 0, 0.0);
    TransformState after = state.afterJoin(false);
    EXPECT_EQ(editLines, after.getLines()) << "gJ lines mismatch";
    EXPECT_EQ(editPos, after.getPos()) << "gJ pos mismatch";
  }

  // J on empty next line
  {
    Lines buf2 = {"hello", "", "world"};
    CursorPos start(0, 2);
    auto [editLines, editPos] = applyViaEdit(buf2, start, "J");

    TransformState state(buf2, start, 0, 0.0);
    TransformState after = state.afterJoin(true);
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
  cerr << "Results: " << res.getResults().size() << endl;
  for (size_t i = 0; i < res.getResults().size(); i++) {
    const auto& r = res.getResults()[i];
    cerr << "  [" << i << "] seq='" << r.getSequence() << "' cost=" << r.getCost() << endl;
    auto nvim = oracle_->simulate(initial, 0, 0, r.getSequence().str());
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
      TransformOptimizer editOpt(config);
      TransformOptimizerParams eparams;

      // Check if pure deletion
      if (d.insertedText.empty()) {
        cerr << "    Pure deletion" << endl;
        TransformResult eres = editOpt.optimizeTransform(
            d.deletedLines(), {}, d.boundary, eparams);
        for (size_t j = 0; j < eres.resultCount(); j++) {
          const auto& bucket = eres.getResults()[j];
          if (!bucket.empty()) {
            cerr << "    pos " << j << ": '" << bucket[0].getSequence()
                 << "' cost=" << bucket[0].getCost() << endl;
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
  cerr << "=== Trace JoinLinesResidual Transform Optimizer ===" << endl;

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
    CharRange r = VimCore::textObjectRange(CursorPos(1,0), buf, false, false, 0, 0, false, false);
    cerr << "  aw range: (" << r.begin.line << "," << r.begin.col
         << ")-(" << r.end.line << "," << r.end.col << ")" << endl;

    // With hasLinesAbove=true (as the edit boundary would have)
    CharRange r2 = VimCore::textObjectRange(CursorPos(1,0), buf, false, false, 0, 0, true, false);
    cerr << "  aw range (hasAbove): (" << r2.begin.line << "," << r2.begin.col
         << ")-(" << r2.end.line << "," << r2.end.col << ")" << endl;
  }

  // Step 4: Run the actual transform optimizer and check all results
  cerr << "\n--- Step 4: Transform optimizer results ---" << endl;
  {
    Lines initial = {"aaa", "xxx", "ccc"};
    Lines goal = {"aaa bbb ccc"};

    // Build boundary from full buffer perspective
    CursorPos beginPos(0, 3);  // After "aaa"
    CursorPos endPos = initial.endPos();
    TransformBoundary boundary(initial, beginPos, endPos);
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
    TransformOptimizer opt(config);
    TransformResult res = opt.optimizeTransform(deletedLines, insertedLines, boundary, {});

    cerr << "  results: " << res.resultCount() << " total" << endl;
    int idx = 0;
    Lines effectiveLines = {"aaa", "xxx", "ccc"};
    for (int r = 0; r < static_cast<int>(deletedLines.size()); r++) {
      for (int c = 0; c < deletedLines[r].effectiveSize(); c++) {
        const auto& bucket = res.getResults()[idx];
        if (!bucket.empty()) {
          const auto& result = bucket[0];
          int fullRow = r + (r == 0 ? static_cast<int>(beginPos.line) : 0);
          // For row 0, col offset is beginPos.col; for others, no offset
          // Actually need to compute proper full-buffer position
          cerr << "  [" << r << "," << c << "] seq='" << result.getSequence()
               << "' cost=" << result.getCost() << endl;
          cerr << "    bytes:";
          for (unsigned char ch : result.getSequence().view()) {
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
  // Failing case from TransformOptimizerOutputCorrectness.SingleLine_Change iter=8
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
  cerr << "OK - nodes=" << result.getStats().nodesExplored() << endl;
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
  TransformBoundary boundary(fullBuffer, CursorPos(0, 2), CursorPos(1, 5));

  cerr << "editRegion=" << editRegion << endl;
  cerr << "boundary: hasPrefix=" << boundary.hasPrefix()
       << " hasSuffix=" << boundary.hasSuffix()
       << " leftColOffset=" << boundary.leftColOffset()
       << " rightColOffset=" << boundary.rightColOffset()
       << " hasLinesAbove=" << boundary.hasLinesAbove()
       << " hasLinesBelow=" << boundary.hasLinesBelow() << endl;

  // Replay the failing sequence.
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
  CursorPos endpoint = VimCore::motionSentenceEndpoint<true, SentenceEdgeType::NextEdge>(
      cursor, editRegion, rightColOffset, hasLinesBelow);
  cerr << "motionSentenceEndpoint from (0,0): (" << endpoint.line << "," << endpoint.col << ")" << endl;
  if (endpoint != POSITION_OUTSIDE_BOUNDARY) {
    cerr << "Explorer d) range: [(" << cursor.line << "," << cursor.col << "), ("
         << endpoint.line << "," << endpoint.col << "))" << endl;
    // Apply the deletion to see what the explorer computes
    Lines explorerBuf = editRegion;
    CursorPos explorerPos = cursor;
    VimCore::deleteRangeAndUpdatePos(
        explorerBuf, CharRange(cursor, endpoint), explorerPos, Mode::Normal);
    cerr << "Explorer d) result: " << explorerBuf << " pos=(" << explorerPos.line << "," << explorerPos.col << ")" << endl;
  }
}
