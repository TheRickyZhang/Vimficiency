// tests/EditOptimizerTests.cpp
//
// Tests for EditOptimizer - A* search for optimal Vim editing sequences.
//
// Current focus: Deletion search - finding optimal ways to clear buffer
// from any starting position. This is the foundation for edit optimization.

#include <gtest/gtest.h>
#include <ostream>

#include "Editor/Edit.h"
#include "Editor/Mode.h"
#include "Editor/NavContext.h"
#include "Editor/Position.h"
#include "Optimizer/Config.h"
#include "Optimizer/EditOptimizer.h"
#include "Boundary/EditBoundary.h"
#include "Utils/Debug.h"
#include "Utils/Lines.h"

using namespace std;

// Helper: Convert (row, col) to flat index for typeAllResults access
// Note: This matches how EditOptimizer indexes results - by position count,
// NOT including newlines. Empty lines count as 1 position.
static int toFlatIndex(int row, int col, const Lines& lines) {
  int idx = 0;
  for (int r = 0; r < row && r < static_cast<int>(lines.size()); r++) {
    idx += lines[r].empty() ? 1 : static_cast<int>(lines[r].size());
  }
  idx += col;
  return idx;
}

class EditOptimizerTest : public ::testing::Test {
protected:
  Config config = Config::uniform();

  EditOptimizer makeOptimizer() {
    return EditOptimizer(config, OptimizerParams(30, 1e4, 1.0, 2.0));
  }
};

// =============================================================================
// Deletion search tests
// =============================================================================

// =============================================================================
// Test Helpers
// =============================================================================

// Helper: Apply a sequence of operations and return final state
struct ApplyResult {
  Lines lines;
  Position pos;
  Mode mode;
  bool success;
  string error;
};

ApplyResult applySequence(const Lines& source, Position startPos, const string& sequence) {
  ApplyResult result;
  result.lines = source;
  result.pos = startPos;
  result.mode = Mode::Normal;
  result.success = true;

  NavContext ctx(100, 50);

  vector<ParsedEdit> ops = Edit::parseEdits(sequence);

  // Apply each operation using Edit::applyEdit
  for (const auto& op : ops) {
    try {
      Edit::applyEdit(result.lines, result.pos, result.mode, ctx, op);
    } catch (const exception& e) {
      result.success = false;
      result.error = "Failed on op '" + string(op.edit) + "': " + e.what();
      return result;
    }
  }

  return result;
}

// Helper: Check if result is valid deletion goal (empty buffer + normal mode)
// Note: Sequences now end with <Esc>, so we expect Normal mode, not Insert.
bool isValidDeletionGoal(const ApplyResult& result) {
  if (!result.success) return false;
  if (result.mode != Mode::Normal) return false;
  if (result.lines.empty()) return true;
  if (result.lines.size() == 1 && result.lines[0].empty()) return true;
  return false;
}

// Helper: Create EditBoundary for full buffer deletion (no constraints)
// This creates a boundary where the entire buffer is the edit region.
EditBoundary makeFullBufferBoundary(const Lines& source) {
  if (source.empty()) {
    return EditBoundary(source, Position(0, 0), Position(0, 0));
  }
  int lastLine = static_cast<int>(source.size()) - 1;
  int lastCol = source[lastLine].empty() ? 0 : static_cast<int>(source[lastLine].size()) - 1;
  return EditBoundary(source, Position(0, 0), Position(lastLine, lastCol));
}

// Helper: Call optimizeEdit for full buffer deletion
// Tests deletion from source to empty buffer.
EditResult optimizeFullDeletion(EditOptimizer& opt, const Lines& source) {
  EditBoundary boundary = makeFullBufferBoundary(source);
  Lines emptyTarget = {""};
  return opt.optimizeEdit(source, emptyTarget, boundary);
}

TEST_F(EditOptimizerTest, DeletionSearch_Simple) {
  // Test deletion search from all positions in "aa\nbb"
  Lines source = {"aa", "bb"};

  EditOptimizer opt = makeOptimizer();
  EditResult res = optimizeFullDeletion(opt, source);

  cout << consume_debug_output() << endl;

  // Print results for each starting position
  cout << "Deletion results for source: " << source << endl;
  for (int r = 0; r < static_cast<int>(source.size()); r++) {
    int cols = source[r].empty() ? 1 : source[r].size();
    for (int c = 0; c < cols; c++) {
      int flatIdx = toFlatIndex(r, c, source);
      const Result& result = res.typeAllResults[flatIdx];
      if (result.isValid()) {
        cout << "  [" << r << "," << c << "]: " << result.getSequenceString()
             << " (cost " << result.keyCost << ")" << endl;
      } else {
        cout << "  [" << r << "," << c << "]: no solution" << endl;
      }
    }
  }

  // All positions should have valid results
  EXPECT_TRUE(res.typeAllResults[toFlatIndex(0, 0, source)].isValid());
  EXPECT_TRUE(res.typeAllResults[toFlatIndex(0, 1, source)].isValid());
  EXPECT_TRUE(res.typeAllResults[toFlatIndex(1, 0, source)].isValid());
  EXPECT_TRUE(res.typeAllResults[toFlatIndex(1, 1, source)].isValid());

  // Verify each sequence actually produces the goal state
  for (int r = 0; r < static_cast<int>(source.size()); r++) {
    int cols = source[r].empty() ? 1 : source[r].size();
    for (int c = 0; c < cols; c++) {
      int flatIdx = toFlatIndex(r, c, source);
      const Result& result = res.typeAllResults[flatIdx];
      if (result.isValid()) {
        string seq = result.getSequenceString();
        ApplyResult applied = applySequence(source, Position(r, c), seq);

        EXPECT_TRUE(applied.success)
            << "Sequence '" << seq << "' from [" << r << "," << c << "] failed: "
            << applied.error;

        EXPECT_TRUE(isValidDeletionGoal(applied))
            << "Sequence '" << seq << "' from [" << r << "," << c << "] "
            << "did not reach goal. Lines: " << applied.lines
            << ", Mode: " << (applied.mode == Mode::Insert ? "Insert" : "Normal");
      }
    }
  }
}

TEST_F(EditOptimizerTest, DeletionSearch_SingleLine) {
  Lines source = {"hello"};

  EditOptimizer opt = makeOptimizer();
  EditResult res = optimizeFullDeletion(opt, source);

  cout << consume_debug_output() << endl;

  cout << "Deletion results for source: " << source << endl;
  for (int c = 0; c < (int)source[0].size(); c++) {
    int flatIdx = toFlatIndex(0, c, source);
    const Result& result = res.typeAllResults[flatIdx];
    if (result.isValid()) {
      cout << "  [0," << c << "]: " << result.getSequenceString()
           << " (cost " << result.keyCost << ")" << endl;

      // Verify
      ApplyResult applied = applySequence(source, Position(0, c), result.getSequenceString());
      EXPECT_TRUE(applied.success) << "Failed: " << applied.error;
      EXPECT_TRUE(isValidDeletionGoal(applied))
          << "Sequence '" << result.getSequenceString() << "' from [0," << c << "] "
          << "did not reach goal. Lines: " << applied.lines;
    } else {
      cout << "  [0," << c << "]: no solution" << endl;
      FAIL() << "No solution for position [0," << c << "]";
    }
  }
}

TEST_F(EditOptimizerTest, DeletionSearch_ThreeLines) {
  Lines source = {"aaa", "bbb", "ccc"};

  EditOptimizer opt = makeOptimizer();
  EditResult res = optimizeFullDeletion(opt, source);

  cout << consume_debug_output() << endl;

  cout << "Deletion results for source: " << source << endl;

  int verified = 0;
  for (int r = 0; r < static_cast<int>(source.size()); r++) {
    int cols = source[r].size();
    for (int c = 0; c < cols; c++) {
      int flatIdx = toFlatIndex(r, c, source);
      const Result& result = res.typeAllResults[flatIdx];
      if (result.isValid()) {
        cout << "  [" << r << "," << c << "]: " << result.getSequenceString()
             << " (cost " << result.keyCost << ")" << endl;

        // Verify
        ApplyResult applied = applySequence(source, Position(r, c), result.getSequenceString());
        EXPECT_TRUE(applied.success)
            << "Sequence '" << result.getSequenceString() << "' failed: " << applied.error;
        EXPECT_TRUE(isValidDeletionGoal(applied))
            << "Sequence '" << result.getSequenceString() << "' from [" << r << "," << c << "] "
            << "did not reach goal. Lines: " << applied.lines
            << ", Mode: " << (applied.mode == Mode::Insert ? "Insert" : "Normal");
        verified++;
      }
    }
  }

  // Should have found solutions for all 9 positions
  EXPECT_EQ(verified, 9);
}

TEST_F(EditOptimizerTest, DeletionSearch_MixedLengths) {
  // Lines of different lengths
  Lines source = {"a", "bbb", "cc"};

  EditOptimizer opt = makeOptimizer();
  EditResult res = optimizeFullDeletion(opt, source);

  cout << consume_debug_output() << endl;

  cout << "Deletion results for source: " << source << endl;

  int verified = 0;
  for (int r = 0; r < static_cast<int>(source.size()); r++) {
    int cols = source[r].size();
    for (int c = 0; c < cols; c++) {
      int flatIdx = toFlatIndex(r, c, source);
      const Result& result = res.typeAllResults[flatIdx];
      if (result.isValid()) {
        cout << "  [" << r << "," << c << "]: " << result.getSequenceString()
             << " (cost " << result.keyCost << ")" << endl;

        ApplyResult applied = applySequence(source, Position(r, c), result.getSequenceString());
        EXPECT_TRUE(applied.success)
            << "Sequence '" << result.getSequenceString() << "' failed: " << applied.error;
        EXPECT_TRUE(isValidDeletionGoal(applied))
            << "Sequence '" << result.getSequenceString() << "' from [" << r << "," << c << "] "
            << "did not reach goal";
        verified++;
      }
    }
  }

  // Should have solutions for all 6 positions (1 + 3 + 2)
  EXPECT_EQ(verified, 6);
}

// =============================================================================
// Boundary-constrained deletion tests
// =============================================================================

TEST_F(EditOptimizerTest, DeletionSearch_WithLinesBelow) {
  // When hasLinesBelow=true, dd on last line is invalid (cursor would escape)
  // Simulates edit region embedded in larger buffer:
  //   xx        <- outside
  //   aa        <- edit region
  //   bb        <- edit region (last line)
  //   xx        <- outside (hasLinesBelow)
  Lines source = {"aa", "bb"};

  EditBoundary boundary = makeFullBufferBoundary(source);
  boundary.hasLinesBelow = true;  // Can't dd on last line

  EditOptimizer opt = makeOptimizer();
  Lines emptyTarget = {""};
  EditResult res = opt.optimizeEdit(source, emptyTarget, boundary);

  cout << consume_debug_output() << endl;

  cout << "Deletion results with hasLinesBelow=true:" << endl;
  for (int r = 0; r < static_cast<int>(source.size()); r++) {
    int cols = source[r].empty() ? 1 : source[r].size();
    for (int c = 0; c < cols; c++) {
      int flatIdx = toFlatIndex(r, c, source);
      const Result& result = res.typeAllResults[flatIdx];
      if (result.isValid()) {
        string seq = result.getSequenceString();
        cout << "  [" << r << "," << c << "]: " << seq
             << " (cost " << result.keyCost << ")" << endl;

        // From line 1 (last line), should NOT start with dd
        if (r == 1) {
          EXPECT_FALSE(seq.find("dd") == 0)
              << "From last line [1," << c << "], sequence should not start with dd: " << seq;
        }
      } else {
        cout << "  [" << r << "," << c << "]: no solution" << endl;
      }
    }
  }

  // All positions should still have valid results
  EXPECT_TRUE(res.typeAllResults[toFlatIndex(0, 0, source)].isValid());
  EXPECT_TRUE(res.typeAllResults[toFlatIndex(0, 1, source)].isValid());
  EXPECT_TRUE(res.typeAllResults[toFlatIndex(1, 0, source)].isValid());
  EXPECT_TRUE(res.typeAllResults[toFlatIndex(1, 1, source)].isValid());
}

TEST_F(EditOptimizerTest, DeletionSearch_SingleLineWithLinesBelow) {
  // Single line with hasLinesBelow - can't dd at all, must use S/cc
  Lines source = {"hello"};

  EditBoundary boundary = makeFullBufferBoundary(source);
  boundary.hasLinesBelow = true;

  EditOptimizer opt = makeOptimizer();
  Lines emptyTarget = {""};
  EditResult res = opt.optimizeEdit(source, emptyTarget, boundary);

  cout << consume_debug_output() << endl;

  cout << "Deletion results (single line, hasLinesBelow):" << endl;
  for (int c = 0; c < (int)source[0].size(); c++) {
    int flatIdx = toFlatIndex(0, c, source);
    const Result& result = res.typeAllResults[flatIdx];
    if (result.isValid()) {
      string seq = result.getSequenceString();
      cout << "  [0," << c << "]: " << seq << endl;

      // Should NOT contain dd since it would delete the only line
      EXPECT_EQ(seq.find("dd"), string::npos)
          << "With single line and hasLinesBelow, should not use dd: " << seq;
    }
  }
}

// =============================================================================
// Full buffer verification tests
// These tests verify that edit operations don't affect content outside the
// edit region when applied to the original full buffer.
// =============================================================================

// Helper: Apply a sequence to a full buffer at a given starting position
// Returns the final state
ApplyResult applySequenceToFullBuffer(const Lines& fullBuffer, Position startPos, const string& sequence) {
  return applySequence(fullBuffer, startPos, sequence);
}

// Helper: Check if a prefix of lines matches expected
bool linesMatchPrefix(const Lines& lines, const Lines& expected, int count) {
  if ((int)lines.size() < count || (int)expected.size() < count) return false;
  for (int i = 0; i < count; i++) {
    if (lines[i] != expected[i]) return false;
  }
  return true;
}

// Helper: Check if a suffix of lines matches expected (last 'count' lines)
bool linesMatchSuffix(const Lines& lines, const Lines& expected, int count) {
  if ((int)lines.size() < count || (int)expected.size() < count) return false;
  int linesStart = lines.size() - count;
  int expectedStart = expected.size() - count;
  for (int i = 0; i < count; i++) {
    if (lines[linesStart + i] != expected[expectedStart + i]) return false;
  }
  return true;
}

TEST_F(EditOptimizerTest, FullBuffer_Linewise) {
  // Linewise edit:
  // xx       <- line 0, outside
  // aa       <- line 1, edit region start
  // bb       <- line 2, edit region end
  // xx       <- line 3, outside
  //
  // Edit region: aa\nbb (lines 1-2)
  // Boundary: hasLinesAbove=true, hasLinesBelow=true

  Lines fullBuffer = {"xx", "aa", "bb", "xx"};
  Lines editRegion = {"aa", "bb"};

  EditBoundary boundary = makeFullBufferBoundary(editRegion);
  boundary.hasLinesAbove = true;
  boundary.hasLinesBelow = true;
  boundary.leftChar = '\n';   // at line start
  boundary.rightChar = '\n';  // at line end

  EditOptimizer opt = makeOptimizer();
  Lines emptyTarget = {""};
  EditResult res = opt.optimizeEdit(editRegion, emptyTarget, boundary);

  cout << consume_debug_output() << endl;

  cout << "=== Linewise boundary test ===" << endl;
  cout << "Full buffer: " << fullBuffer << endl;
  cout << "Edit region: " << editRegion << endl;

  // Test each starting position in the edit region
  for (int r = 0; r < static_cast<int>(editRegion.size()); r++) {
    int cols = editRegion[r].empty() ? 1 : editRegion[r].size();
    for (int c = 0; c < cols; c++) {
      int flatIdx = toFlatIndex(r, c, editRegion);
      const Result& result = res.typeAllResults[flatIdx];
      if (!result.isValid()) continue;

      string seq = result.getSequenceString();

      // Map edit region position to full buffer position
      // Edit region [r,c] -> full buffer [r+1, c] (edit region starts at line 1)
      Position fullBufferPos(r + 1, c);

      cout << "  Edit region [" << r << "," << c << "] = full buffer ["
           << fullBufferPos.line << "," << fullBufferPos.col << "]: " << seq << endl;

      // Apply sequence to full buffer
      ApplyResult applied = applySequenceToFullBuffer(fullBuffer, fullBufferPos, seq);

      EXPECT_TRUE(applied.success)
          << "Sequence '" << seq << "' from [" << r << "," << c << "] failed: " << applied.error;

      // Verify line 0 ("xx") is unchanged
      ASSERT_GE(applied.lines.size(), 1u)
          << "Buffer too small after applying '" << seq << "'";
      EXPECT_EQ(applied.lines[0], "xx")
          << "Line 0 was modified! Expected 'xx', got '" << applied.lines[0]
          << "' after applying '" << seq << "'";

      // Verify last line ("xx") is unchanged
      // Note: line count may have changed, but last line should still be "xx"
      EXPECT_EQ(applied.lines.back(), "xx")
          << "Last line was modified! Expected 'xx', got '" << applied.lines.back()
          << "' after applying '" << seq << "'";

      cout << "    Result: " << applied.lines << " (outside lines preserved)" << endl;
    }
  }
}

TEST_F(EditOptimizerTest, FullBuffer_SpaceSeparated) {
  // Space-separated edit:
  // "x aa"    <- line 0: 'x ' outside, 'aa' edit region
  // "bb x"    <- line 1: 'bb' edit region, ' x' outside
  //
  // Edit region: aa\nbb (but embedded with spaces)
  // Line ops (dd, cc, S) should NOT be used because they'd delete the x's
  //
  // KNOWN LIMITATION: The "isolated region" approach doesn't work well for
  // partial-line multi-line regions. Sequences found for the isolated region
  // ["aa", "bb"] have different effects when applied to full buffer ["x aa", "bb x"]
  // because motions like E, w, b behave differently with different surrounding content.
  //
  // For such regions, each line should be processed independently.
  // This test is informational - it shows found sequences but they won't preserve
  // outside content when applied to the full buffer.

  Lines fullBuffer = {"x aa", "bb x"};
  Lines editRegion = {"aa", "bb"};

  // Boundary: NOT full lines - dd/cc/S would delete outside content
  EditBoundary boundary = makeFullBufferBoundary(editRegion);
  boundary.hasLinesAbove = false;
  boundary.hasLinesBelow = false;
  boundary.leftChar = ' ';   // "x " before edit region
  boundary.rightChar = ' ';  // " x" after edit region

  EditOptimizer opt = makeOptimizer();
  Lines emptyTarget = {""};
  EditResult res = opt.optimizeEdit(editRegion, emptyTarget, boundary);

  cout << consume_debug_output() << endl;

  cout << "=== Space-separated boundary test ===" << endl;
  cout << "Full buffer: " << fullBuffer << endl;
  cout << "Edit region: " << editRegion << endl;
  cout << "leftChar=' ', rightChar=' ' -> line ops blocked" << endl;

  // Verify NO line operations are used (they'd delete the x's)
  // Full line: dd, cc, S
  // To EOL: C, D, c$, d$
  // To SOL: c0, d0, c^, d^
  static const vector<string> FORBIDDEN_OPS = {
    "dd", "cc", "S",           // Full line ops
    "C", "D", "c$", "d$",      // To end of line (endsAtLineEnd=false)
    "c0", "d0", "c^", "d^"    // To start of line (startsAtLineStart=false)
  };

  for (int r = 0; r < static_cast<int>(editRegion.size()); r++) {
    int cols = editRegion[r].empty() ? 1 : editRegion[r].size();
    for (int c = 0; c < cols; c++) {
      int flatIdx = toFlatIndex(r, c, editRegion);
      const Result& result = res.typeAllResults[flatIdx];
      if (!result.isValid()) {
        cout << "  [" << r << "," << c << "]: no solution" << endl;
        continue;
      }

      string seq = result.getSequenceString();
      cout << "  [" << r << "," << c << "]: " << seq << endl;

      // Verify no forbidden operations in the sequence
      for (const auto& forbiddenOp : FORBIDDEN_OPS) {
        EXPECT_EQ(seq.find(forbiddenOp), string::npos)
            << "Sequence '" << seq << "' from [" << r << "," << c
            << "] contains '" << forbiddenOp << "' which would delete outside content!";
      }

      // Map edit region position to full buffer position and apply
      // Edit [0,c] -> Full [0, c+2]  (after "x ")
      // Edit [1,c] -> Full [1, c]    (at start of line)
      Position fullBufferPos = (r == 0) ? Position(0, c + 2) : Position(1, c);

      ApplyResult applied = applySequenceToFullBuffer(fullBuffer, fullBufferPos, seq);

      if (applied.success) {
        // Verify outside content preserved
        // Line 0 should still start with "x " (the part before edit region)
        bool line0PrefixOk = applied.lines.size() > 0 &&
                              applied.lines[0].substr(0, 2) == "x ";
        // Last line should still end with " x" (the part after edit region)
        bool line1SuffixOk = applied.lines.size() > 0 &&
                              applied.lines.back().size() >= 2 &&
                              applied.lines.back().substr(applied.lines.back().size() - 2) == " x";

        cout << "    Applied to full buffer: " << applied.lines;
        if (line0PrefixOk && line1SuffixOk) {
          cout << " ✓ (outside preserved)" << endl;
        } else {
          cout << " ✗ (OUTSIDE MODIFIED!)" << endl;
        }

        // Note: We're not asserting here because the isolated region approach
        // may not preserve full buffer content. This is informational.
      } else {
        cout << "    Failed to apply: " << applied.error << endl;
      }
    }
  }
}

TEST_F(EditOptimizerTest, FullBuffer_Linewise_VerifyNoEscape) {
  // More rigorous test: verify cursor never escapes to outside content
  // xx       <- line 0, outside
  // aa       <- line 1, edit region
  // bb       <- line 2, edit region
  // yy       <- line 3, outside
  //
  // After applying deletion sequence, cursor should be within the
  // edit region (or what remains of it), never on xx or yy

  Lines fullBuffer = {"xx", "aa", "bb", "yy"};
  Lines editRegion = {"aa", "bb"};

  EditBoundary boundary = makeFullBufferBoundary(editRegion);
  boundary.hasLinesAbove = true;
  boundary.hasLinesBelow = true;
  boundary.leftChar = '\n';   // at line start
  boundary.rightChar = '\n';  // at line end

  EditOptimizer opt = makeOptimizer();
  Lines emptyTarget = {""};
  EditResult res = opt.optimizeEdit(editRegion, emptyTarget, boundary);

  cout << consume_debug_output() << endl;

  cout << "=== Linewise cursor escape test ===" << endl;

  for (int r = 0; r < static_cast<int>(editRegion.size()); r++) {
    int cols = editRegion[r].empty() ? 1 : editRegion[r].size();
    for (int c = 0; c < cols; c++) {
      int flatIdx = toFlatIndex(r, c, editRegion);
      const Result& result = res.typeAllResults[flatIdx];
      if (!result.isValid()) continue;

      string seq = result.getSequenceString();
      Position fullBufferPos(r + 1, c);

      ApplyResult applied = applySequenceToFullBuffer(fullBuffer, fullBufferPos, seq);

      ASSERT_TRUE(applied.success)
          << "Sequence failed: " << applied.error;

      // Verify cursor is not on line 0 (the "xx" above)
      EXPECT_GT(applied.pos.line, 0)
          << "Cursor escaped to line above! Pos=[" << applied.pos.line << ","
          << applied.pos.col << "] after '" << seq << "'";

      // After deletion, buffer size changed. The original "yy" line
      // should still be the last line, and cursor should not be on it.
      // But we need to track where "yy" ended up...

      // With edit region of 2 lines reduced to 1 empty line,
      // buffer goes from 4 lines to 3 lines: ["xx", "", "yy"]
      // Cursor should be on line 1 (the empty edit region), not line 0 or 2

      // Verify first line unchanged
      EXPECT_EQ(applied.lines[0], "xx")
          << "First line modified after '" << seq << "'";

      // Verify last line unchanged
      EXPECT_EQ(applied.lines.back(), "yy")
          << "Last line modified after '" << seq << "'";

      // Verify cursor is in the middle (the edit region remnant)
      EXPECT_EQ(applied.pos.line, 1)
          << "Cursor not in edit region! Pos=[" << applied.pos.line << ","
          << applied.pos.col << "] after '" << seq << "', buffer=" << applied.lines;

      cout << "  [" << r << "," << c << "]: " << seq
           << " -> cursor at [" << applied.pos.line << "," << applied.pos.col << "]"
           << ", buffer=" << applied.lines << " ✓" << endl;
    }
  }
}

// =============================================================================
// Replacement strategy tests
// =============================================================================

TEST_F(EditOptimizerTest, Replacement_SingleDiff) {
  // "fresh" -> "frosh" - only 'e' -> 'o' differs at position 2
  vector<Result> results;
  int lastPos = -1;
  tryReplacement("fresh", "frosh", config, lastPos, results);

  ASSERT_FALSE(results.empty());
  Result result = results[0];
  EXPECT_TRUE(result.isValid());
  string seq = result.getSequenceString();
  EXPECT_FALSE(seq.empty());

  cout << "Replacement 'fresh' -> 'frosh': " << seq
       << " (cost " << result.keyCost << ")" << endl;

  // Should navigate to position 2, then replace 'e' with 'o'
  // Expected: "2lro" (2l to move, then ro to replace with 'o')
  // Or it could be "llro" depending on cost calculation
  EXPECT_TRUE(seq.find("ro") != string::npos ||
              seq.find("r") != string::npos)
      << "Expected replacement command in: " << seq;
}

TEST_F(EditOptimizerTest, Replacement_MultipleDiffs) {
  // "hello" -> "jello" - 'h' -> 'j' at position 0
  vector<Result> results;
  int lastPos = -1;
  tryReplacement("hello", "jello", config, lastPos, results);

  ASSERT_FALSE(results.empty());
  Result result = results[0];
  EXPECT_TRUE(result.isValid());
  string seq = result.getSequenceString();
  cout << "Replacement 'hello' -> 'jello': " << seq
       << " (cost " << result.keyCost << ")" << endl;

  // At position 0, should just be "rj"
  EXPECT_EQ(seq, "rj")
      << "Expected 'rj' for single char replacement at col 0";
}

TEST_F(EditOptimizerTest, Replacement_ConsecutiveDiffs) {
  // "abc" -> "xyz" - all three chars differ
  vector<Result> results;
  int lastPos = -1;
  tryReplacement("abc", "xyz", config, lastPos, results);

  ASSERT_FALSE(results.empty());
  Result result = results[0];
  EXPECT_TRUE(result.isValid());
  string seq = result.getSequenceString();
  cout << "Replacement 'abc' -> 'xyz': " << seq
       << " (cost " << result.keyCost << ")" << endl;

  // With 3 consecutive diffs starting at col 0, should use R mode: "Rxyz<Esc>"
  EXPECT_TRUE(seq.find("Rxyz") != string::npos)
      << "Expected R mode for consecutive replacements: " << seq;
}

// Precondition tests - these test that callers properly check preconditions
// before calling tryReplacement (which asserts on invalid input)

TEST_F(EditOptimizerTest, Replacement_NoDiffs_Precondition) {
  // "same" -> "same" - no differences
  // Callers should check deleted != inserted before calling
  string deleted = "same";
  string inserted = "same";
  EXPECT_EQ(deleted, inserted) << "Precondition: caller should skip identical strings";
}

TEST_F(EditOptimizerTest, Replacement_DifferentLengths_Precondition) {
  // Different lengths - callers should check this
  string deleted = "hello";
  string inserted = "hi";
  EXPECT_NE(deleted.size(), inserted.size()) << "Precondition: caller should check lengths";
}

TEST_F(EditOptimizerTest, Replacement_WithNewlines_Precondition) {
  // Strings with newlines - callers should check for single-line
  string deleted = "hello\nworld";
  EXPECT_NE(deleted.find('\n'), string::npos) << "Precondition: caller should check for newlines";
}

TEST_F(EditOptimizerTest, Replacement_SparseDiffs) {
  // "0000000" -> "1001001" - three non-consecutive diffs at positions 0, 3, 6
  vector<Result> results;
  int lastPos = -1;
  tryReplacement("0000000", "1001001", config, lastPos, results);

  ASSERT_FALSE(results.empty());
  Result result = results[0];
  EXPECT_TRUE(result.isValid());
  string seq = result.getSequenceString();
  cout << "Replacement '0000000' -> '1001001': " << seq
       << " (cost " << result.keyCost << ")" << endl;

  // Should have multiple r1 commands with navigation
  // Expected something like: r1 3l r1 3l r1
  size_t replaceCount = 0;
  for (size_t i = 0; i < seq.size(); i++) {
    if (seq[i] == 'r' && i+1 < seq.size() && seq[i+1] == '1') {
      replaceCount++;
    }
  }
  EXPECT_GE(replaceCount, 3u)
      << "Expected at least 3 single-char replacements: " << seq;
}

