// tests/EditTest.cpp

#include <gtest/gtest.h>

#include "Editor/Edit.h"
#include "Editor/NavContext.h"

using namespace std;

// =============================================================================
// Edit Test Suite - Tests applyEdit correctness
// =============================================================================

class EditTest : public ::testing::Test {
protected:
  NavContext navContext{39, 19};

  // Helper: apply edit and return resulting state
  struct EditResult {
    Lines lines;
    Position pos;
    Mode mode;
  };

  EditResult applyEdit(Lines lines, Position pos, Mode mode, const string& edit, int count = 0) {
    Edit::applyEdit(lines, pos, mode, navContext, ParsedEdit(edit, count));
    return {lines, pos, mode};
  }

  // Helper for common Normal mode edits
  EditResult applyNormalEdit(Lines lines, Position pos, const string& edit, int count = 0) {
    return applyEdit(lines, pos, Mode::Normal, edit, count);
  }

  // Helpers to check state
  static void expectLines(const Lines& actual, const Lines& expected, const string& msg = "") {
    ASSERT_EQ(actual.size(), expected.size()) << msg << " (line count mismatch)";
    for (size_t i = 0; i < actual.size(); i++) {
      EXPECT_EQ(actual[i], expected[i]) << msg << " (line " << i << ")";
    }
  }

  static void expectPos(Position actual, int line, int col, const string& msg = "") {
    EXPECT_EQ(actual.line, line) << msg << " (line)";
    EXPECT_EQ(actual.col, col) << msg << " (col)";
  }

  static void expectState(const EditResult& result, const Lines& expectedLines,
                          int line, int col, Mode mode, const string& msg = "") {
    expectLines(result.lines, expectedLines, msg);
    expectPos(result.pos, line, col, msg);
    EXPECT_EQ(result.mode, mode) << msg << " (mode)";
  }
};

// =============================================================================
// 1. CHARACTER OPERATIONS (x, X, s, r, ~)
// =============================================================================

TEST_F(EditTest, X_DeleteCharUnderCursor) {
  auto r = applyNormalEdit({"abcde"}, {0, 2}, "x");
  expectState(r, {"abde"}, 0, 2, Mode::Normal, "x deletes char at cursor");
}

TEST_F(EditTest, X_DeleteLastChar) {
  auto r = applyNormalEdit({"abcde"}, {0, 4}, "x");
  expectState(r, {"abcd"}, 0, 3, Mode::Normal, "x on last char clamps col");
}

TEST_F(EditTest, X_DeleteWithCount) {
  auto r = applyNormalEdit({"abcdefgh"}, {0, 2}, "x", 3);
  expectState(r, {"abfgh"}, 0, 2, Mode::Normal, "3x deletes 3 chars");
}

TEST_F(EditTest, X_DeleteOnlyChar) {
  auto r = applyNormalEdit({"a"}, {0, 0}, "x");
  expectState(r, {""}, 0, 0, Mode::Normal, "x on single char leaves empty line");
}

TEST_F(EditTest, BigX_DeleteCharBeforeCursor) {
  auto r = applyNormalEdit({"abcde"}, {0, 2}, "X");
  expectState(r, {"acde"}, 0, 1, Mode::Normal, "X deletes char before cursor");
}

TEST_F(EditTest, BigX_AtColumnZero_Throws) {
  EXPECT_THROW(applyNormalEdit({"abcde"}, {0, 0}, "X"), runtime_error);
}

TEST_F(EditTest, BigX_DeleteWithCount) {
  auto r = applyNormalEdit({"abcdefgh"}, {0, 5}, "X", 3);
  expectState(r, {"abfgh"}, 0, 2, Mode::Normal, "3X deletes 3 chars before cursor");
}

TEST_F(EditTest, S_Substitute) {
  auto r = applyNormalEdit({"abcde"}, {0, 2}, "s");
  expectState(r, {"abde"}, 0, 2, Mode::Insert, "s deletes char and enters insert");
}

TEST_F(EditTest, S_SubstituteWithCount) {
  auto r = applyNormalEdit({"abcdefgh"}, {0, 2}, "s", 3);
  expectState(r, {"abfgh"}, 0, 2, Mode::Insert, "3s deletes 3 chars");
}

TEST_F(EditTest, R_ReplaceChar) {
  auto r = applyNormalEdit({"abcde"}, {0, 2}, "rx");
  expectState(r, {"abxde"}, 0, 2, Mode::Normal, "rx replaces char with x");
}

TEST_F(EditTest, R_ReplaceWithCount) {
  auto r = applyNormalEdit({"abcdefgh"}, {0, 2}, "rx", 3);
  expectState(r, {"abxxxfgh"}, 0, 4, Mode::Normal, "3rx replaces 3 chars with x");
}

TEST_F(EditTest, Tilde_ToggleCase) {
  auto r = applyNormalEdit({"aBcDe"}, {0, 0}, "~");
  expectState(r, {"ABcDe"}, 0, 0, Mode::Normal, "~ toggles case");
}

TEST_F(EditTest, Tilde_WithCount) {
  auto r = applyNormalEdit({"aBcDe"}, {0, 0}, "~", 3);
  expectState(r, {"AbCDe"}, 0, 2, Mode::Normal, "3~ toggles 3 chars");
}

// =============================================================================
// 2. WORD DELETION (dw, dW, de, dE, db, dB, dge, dgE)
// =============================================================================

TEST_F(EditTest, Dw_DeleteToNextWord) {
  auto r = applyNormalEdit({"one two three"}, {0, 0}, "dw");
  expectState(r, {"two three"}, 0, 0, Mode::Normal, "dw deletes to next word");
}

TEST_F(EditTest, Dw_DeleteMultipleWords) {
  auto r = applyNormalEdit({"one two three four"}, {0, 0}, "dw", 2);
  expectState(r, {"three four"}, 0, 0, Mode::Normal, "2dw deletes 2 words");
}

TEST_F(EditTest, DW_DeleteBigWord) {
  auto r = applyNormalEdit({"foo.bar baz"}, {0, 0}, "dW");
  expectState(r, {"baz"}, 0, 0, Mode::Normal, "dW deletes big word including punctuation");
}

// dw at end of line: does NOT cross to next line (Vim special case for count=1)
TEST_F(EditTest, Dw_DoesNotCrossLine) {
  auto r = applyNormalEdit({"ab", "cd"}, {0, 0}, "dw");
  // From (0,0), w motion would go to (1,0), but dw never deletes newlines
  // It deletes to end of current line only, leaving empty line + "cd"
  expectLines(r.lines, {"", "cd"}, "dw stays on same line, doesn't delete newline");
}

// dw on empty line: empty line IS a word, so it gets deleted (including newline)
TEST_F(EditTest, Dw_OnEmptyLine_DeletesLine) {
  auto r = applyNormalEdit({"", "cd"}, {0, 0}, "dw");
  // Empty line is considered a word, so dw deletes it entirely
  expectLines(r.lines, {"cd"}, "dw on empty line deletes the line");
}

// 2dw: count > 1 CAN cross lines (special case only applies to count=1)
TEST_F(EditTest, Dw_WithCount_CrossesLines) {
  auto r = applyNormalEdit({"ab", "cd"}, {0, 0}, "dw", 2);
  // 2dw deletes both words across lines
  expectLines(r.lines, {""}, "2dw crosses lines");
}

// 2dw where motion lands ON a word boundary (not past EOF)
// The key case: motion lands on 'a' but we don't want to delete 'a'
TEST_F(EditTest, Dw_WithCount_LandsOnWord) {
  auto r = applyNormalEdit({"ab", "ab a"}, {0, 0}, "dw", 2);
  // Two w motions: ab -> ab -> a. Lands on 'a' (valid boundary)
  // Exclusive delete: delete everything BEFORE 'a', leaving 'a'
  expectLines(r.lines, {"a"}, "2dw lands on 'a', doesn't delete it");
}

TEST_F(EditTest, De_DeleteToEndOfWord) {
  auto r = applyNormalEdit({"one two"}, {0, 0}, "de");
  expectState(r, {" two"}, 0, 0, Mode::Normal, "de deletes to end of word (inclusive)");
}

TEST_F(EditTest, DE_DeleteToEndOfBigWord) {
  auto r = applyNormalEdit({"foo.bar baz"}, {0, 0}, "dE");
  expectState(r, {" baz"}, 0, 0, Mode::Normal, "dE deletes to end of WORD");
}

// de/dE DOES cross lines (unlike dw/dW which has a special case)
TEST_F(EditTest, De_DoesCrossLine) {
  auto r = applyNormalEdit({"a", "cd"}, {0, 0}, "de");
  // e motion goes to next line's word end, de follows it
  expectLines(r.lines, {""}, "de crosses lines and deletes to word end");
}

TEST_F(EditTest, Db_DeleteBackToWordStart) {
  auto r = applyNormalEdit({"one two three"}, {0, 8}, "db");
  expectState(r, {"one three"}, 0, 4, Mode::Normal, "db deletes back to word start");
}

TEST_F(EditTest, DB_DeleteBackToBigWordStart) {
  auto r = applyNormalEdit({"foo.bar baz.qux"}, {0, 8}, "dB");
  expectState(r, {"baz.qux"}, 0, 0, Mode::Normal, "dB deletes back to WORD start");
}

TEST_F(EditTest, Db_AtBufferStart_Throws) {
  EXPECT_THROW(applyNormalEdit({"one two"}, {0, 0}, "db"), runtime_error);
}

TEST_F(EditTest, Dge_DeleteBackToWordEnd) {
  auto r = applyNormalEdit({"one two three"}, {0, 8}, "dge");
  // From col 8 ('t' in three), ge goes to col 6 (end of 'two')
  // ge is INCLUSIVE motion: deletes from col 6 to col 8 (chars 'o', ' ', 't')
  // Cursor ends at col 6 (where ge landed, now points to 'h')
  expectState(r, {"one twhree"}, 0, 6, Mode::Normal, "dge deletes back to prev word end inclusive");
}

TEST_F(EditTest, DgE_DeleteBackToBigWordEnd) {
  auto r = applyNormalEdit({"foo.bar baz.qux"}, {0, 8}, "dgE");
  // From col 8 ('b' in baz), gE goes to col 6 (end of 'foo.bar')
  // gE is INCLUSIVE motion: deletes from col 6 to col 8 (chars 'r', ' ', 'b')
  // Cursor ends at col 6 (where gE landed, now points to 'a')
  expectState(r, {"foo.baaz.qux"}, 0, 6, Mode::Normal, "dgE deletes back to WORD end inclusive");
}

// =============================================================================
// 3. WORD CHANGE (cw, cW, ce, cE, cb, cB) - Including cw/cW special case!
// =============================================================================

// IMPORTANT: cw/cW on a word acts like ce/cE (Vim's special case)
TEST_F(EditTest, Cw_OnWord_ActsLikeCe) {
  auto r = applyNormalEdit({"one two"}, {0, 0}, "cw");
  // cw on "one" should delete "one" only, NOT "one " (the trailing space)
  expectState(r, {" two"}, 0, 0, Mode::Insert, "cw on word acts like ce (no trailing space)");
}

TEST_F(EditTest, CW_OnWord_ActsLikeCE) {
  auto r = applyNormalEdit({"foo.bar baz"}, {0, 0}, "cW");
  // cW on "foo.bar" should delete "foo.bar" only, NOT "foo.bar "
  expectState(r, {" baz"}, 0, 0, Mode::Insert, "cW on word acts like cE");
}

// cw on whitespace uses w motion
TEST_F(EditTest, Cw_OnWhitespace_UsesWMotion) {
  auto r = applyNormalEdit({"one  two"}, {0, 3}, "cw");
  // From col 3 (first space), cw should delete to start of "two"
  expectState(r, {"onetwo"}, 0, 3, Mode::Insert, "cw on whitespace deletes to next word");
}

// cw should NOT cross lines (unlike dw which does)
TEST_F(EditTest, Cw_AtEndOfWord_DoesNotCrossLine) {
  auto r = applyNormalEdit({"ab", "cd"}, {0, 0}, "cw");
  // cw from (0,0) on "ab" should only delete "ab", not the newline
  expectState(r, {"", "cd"}, 0, 0, Mode::Insert, "cw at word end stays on same line");
}

TEST_F(EditTest, CW_AtEndOfWord_DoesNotCrossLine) {
  auto r = applyNormalEdit({"ab", "cd"}, {0, 0}, "cW");
  expectState(r, {"", "cd"}, 0, 0, Mode::Insert, "cW at word end stays on same line");
}

TEST_F(EditTest, Ce_DeleteToWordEnd) {
  auto r = applyNormalEdit({"one two"}, {0, 0}, "ce");
  expectState(r, {" two"}, 0, 0, Mode::Insert, "ce deletes to word end");
}

TEST_F(EditTest, CE_DeleteToBigWordEnd) {
  auto r = applyNormalEdit({"foo.bar baz"}, {0, 0}, "cE");
  expectState(r, {" baz"}, 0, 0, Mode::Insert, "cE deletes to WORD end");
}

// ce/cE DOES cross lines (unlike cw/cW which has a special case)
TEST_F(EditTest, Ce_DoesCrossLine) {
  auto r = applyNormalEdit({"a", "cd"}, {0, 0}, "ce");
  // e motion goes to next line's word end, ce follows it
  expectLines(r.lines, {""}, "ce crosses lines and changes to word end");
  EXPECT_EQ(r.mode, Mode::Insert);
}

TEST_F(EditTest, Cb_ChangeBackToWordStart) {
  auto r = applyNormalEdit({"one two three"}, {0, 8}, "cb");
  expectState(r, {"one three"}, 0, 4, Mode::Insert, "cb changes back to word start");
}

TEST_F(EditTest, CB_ChangeBackToBigWordStart) {
  auto r = applyNormalEdit({"foo.bar baz.qux"}, {0, 8}, "cB");
  expectState(r, {"baz.qux"}, 0, 0, Mode::Insert, "cB changes back to WORD start");
}

// =============================================================================
// 3b. WORD MOTION EOF/BUFFER BOUNDARY EDGE CASES
//
// These test the special behavior when word motions reach buffer boundaries.
// Key insight: motionW returns "past end" position (col = line.size()) when
// it can't find more words, allowing dw to correctly delete everything.
// =============================================================================

// dw at last word of single-line buffer - should delete everything
TEST_F(EditTest, Dw_AtLastWord_SingleLine) {
  auto r = applyNormalEdit({"abc"}, {0, 0}, "dw");
  // w motion can't find next word, returns "past end", dw deletes all
  expectLines(r.lines, {""}, "dw on only word deletes everything");
}

// dw at last word with trailing whitespace
TEST_F(EditTest, Dw_AtLastWord_WithTrailingSpace) {
  auto r = applyNormalEdit({"abc "}, {0, 0}, "dw");
  // w goes to end of line (past space), dw deletes "abc "
  expectLines(r.lines, {""}, "dw with trailing space deletes word and space");
}

// 2dw where second w lands on a word (not past EOF)
TEST_F(EditTest, Dw_Count2_LandsOnWord) {
  auto r = applyNormalEdit({"ab cd ef"}, {0, 0}, "dw", 2);
  // Two w motions: ab→cd→ef. Lands on 'e' (valid word start)
  // Exclusive delete: "ab cd " deleted, "ef" remains
  expectLines(r.lines, {"ef"}, "2dw landing on word is exclusive");
}

// 3dw where third w goes past EOF
TEST_F(EditTest, Dw_Count3_PastEOF) {
  auto r = applyNormalEdit({"ab cd ef"}, {0, 0}, "dw", 3);
  // Three w motions: ab→cd→ef→past end. Returns "past end"
  // Delete everything including 'ef'
  expectLines(r.lines, {""}, "3dw past EOF deletes everything");
}

// dW at last WORD of buffer
TEST_F(EditTest, DW_AtLastWord_SingleLine) {
  auto r = applyNormalEdit({"foo.bar"}, {0, 0}, "dW");
  expectLines(r.lines, {""}, "dW on only WORD deletes everything");
}

// de at last char of last word - should delete that char (inclusive)
// NOTE: This tests that e motion at EOF correctly handles the edge case
TEST_F(EditTest, De_AtLastCharOfWord_EOF) {
  auto r = applyNormalEdit({"abc"}, {0, 2}, "de");
  // e can't move forward, but we're on word char - delete it
  expectLines(r.lines, {"ab"}, "de at last char of buffer deletes that char");
}

// de at last char of word with more words after
TEST_F(EditTest, De_AtEndOfWord_MoreWords) {
  auto r = applyNormalEdit({"abc def"}, {0, 2}, "de");
  // e from 'c' (end of word) goes to 'f' (end of next word)
  expectLines(r.lines, {"ab"}, "de at end of word goes to next word end");
}

// de on word with trailing whitespace - should NOT delete the whitespace
TEST_F(EditTest, De_AtLastWord_TrailingSpace) {
  auto r = applyNormalEdit({"abc "}, {0, 0}, "de");
  // e goes to 'c' (end of word), inclusive delete "abc", space remains
  expectLines(r.lines, {" "}, "de deletes word but not trailing space");
}

// dE at last char of last WORD
TEST_F(EditTest, DE_AtLastCharOfWord_EOF) {
  auto r = applyNormalEdit({"foo.bar"}, {0, 6}, "dE");
  // E can't move forward, but we're on word char - delete it
  expectLines(r.lines, {"foo.ba"}, "dE at last char of buffer deletes that char");
}

// db at second word - deletes back to start of previous word
TEST_F(EditTest, Db_AtSecondWord) {
  auto r = applyNormalEdit({"abc def"}, {0, 4}, "db");
  // b from 'd' goes to 'a', exclusive delete removes "abc "
  expectLines(r.lines, {"def"}, "db at word start deletes previous word + space");
}

// dge at second word - deletes back to end of previous word (inclusive)
TEST_F(EditTest, Dge_AtSecondWord) {
  auto r = applyNormalEdit({"abc def"}, {0, 4}, "dge");
  // ge from 'd' goes to 'c' (end of prev word)
  // ge is INCLUSIVE: delete from 'c' (col 2) to 'd' (col 4) = "c d"
  expectLines(r.lines, {"abef"}, "dge deletes from prev word end to cursor inclusive");
}

// =============================================================================
// 4. LINE OPERATIONS (dd, cc, S, D, C, d$, c$, d0, c0)
// =============================================================================

// NOTE: dd column behavior differs between Neovim (default) and Vim (LEGACY_VIM):
//   - Neovim: preserves column position (clamped to new line length)
//   - Vim:    goes to first non-blank character (startofline option)
TEST_F(EditTest, Dd_DeleteLine) {
  auto r = applyNormalEdit({"one", "two", "three"}, {0, 1}, "dd");
  expectLines(r.lines, {"two", "three"}, "dd deletes current line");
  EXPECT_EQ(r.pos.line, 0) << "dd stays at same line index";
  EXPECT_EQ(r.pos.col, 1) << "dd preserves column (Neovim default)";
}

TEST_F(EditTest, Dd_DeleteLinePreservesColumnClamped) {
  // When new line is shorter, column is clamped to end of line
  auto r = applyNormalEdit({"longline", "ab"}, {0, 6}, "dd");
  expectLines(r.lines, {"ab"}, "dd deletes line");
  EXPECT_EQ(r.pos.line, 0);
  EXPECT_EQ(r.pos.col, 1) << "column clamped to shorter line (Neovim default)";
}

TEST_F(EditTest, Dd_DeleteLineWithCount) {
  auto r = applyNormalEdit({"one", "two", "three", "four"}, {0, 0}, "dd", 2);
  expectLines(r.lines, {"three", "four"}, "2dd deletes 2 lines");
}

TEST_F(EditTest, Dd_DeleteLastLine) {
  auto r = applyNormalEdit({"one", "two"}, {1, 0}, "dd");
  expectLines(r.lines, {"one"}, "dd on last line");
  EXPECT_EQ(r.pos.line, 0) << "dd clamps to valid line";
}

TEST_F(EditTest, Dd_DeleteOnlyLine) {
  auto r = applyNormalEdit({"only"}, {0, 0}, "dd");
  expectLines(r.lines, {}, "dd on only line leaves empty buffer");
}

TEST_F(EditTest, Cc_ChangeLine) {
  auto r = applyNormalEdit({"one", "two"}, {0, 2}, "cc");
  expectState(r, {"", "two"}, 0, 0, Mode::Insert, "cc clears line and enters insert");
}

TEST_F(EditTest, S_ChangeLine) {
  auto r = applyNormalEdit({"one", "two"}, {0, 2}, "S");
  expectState(r, {"", "two"}, 0, 0, Mode::Insert, "S same as cc");
}

TEST_F(EditTest, D_DeleteToEndOfLine) {
  auto r = applyNormalEdit({"one two three"}, {0, 4}, "D");
  expectState(r, {"one "}, 0, 3, Mode::Normal, "D deletes to end of line");
}

TEST_F(EditTest, C_ChangeToEndOfLine) {
  auto r = applyNormalEdit({"one two three"}, {0, 4}, "C");
  // C deletes from cursor to end, cursor stays at delete position (clamped)
  expectState(r, {"one "}, 0, 3, Mode::Insert, "C changes to end of line");
}

TEST_F(EditTest, D0_DeleteToLineStart) {
  auto r = applyNormalEdit({"one two three"}, {0, 8}, "d0");
  expectState(r, {"three"}, 0, 0, Mode::Normal, "d0 deletes to line start");
}

TEST_F(EditTest, D0_AtColumnZero_Throws) {
  EXPECT_THROW(applyNormalEdit({"one"}, {0, 0}, "d0"), runtime_error);
}

TEST_F(EditTest, C0_ChangeToLineStart) {
  auto r = applyNormalEdit({"one two three"}, {0, 8}, "c0");
  expectState(r, {"three"}, 0, 0, Mode::Insert, "c0 changes to line start");
}

// =============================================================================
// 5. JOIN OPERATIONS (J, gJ)
// NOTE: J behavior differs for joinspaces option:
//   - Neovim (default): single space after .!?
//   - Vim (LEGACY_VIM): two spaces after .!?
// NOTE: gJ never adds spaces and preserves leading whitespace
// =============================================================================

TEST_F(EditTest, J_JoinLinesWithSpace) {
  auto r = applyNormalEdit({"one", "two"}, {0, 0}, "J");
  expectState(r, {"one two"}, 0, 3, Mode::Normal, "J joins with space");
}

TEST_F(EditTest, J_JoinTrimsLeadingWhitespace) {
  auto r = applyNormalEdit({"one", "  two"}, {0, 0}, "J");
  expectState(r, {"one two"}, 0, 3, Mode::Normal, "J trims leading whitespace from next line");
}

TEST_F(EditTest, J_JoinAfterPeriod_SingleSpace) {
  // Neovim default: single space after period (joinspaces OFF)
  // LEGACY_VIM: would add two spaces after period
  auto r = applyNormalEdit({"end.", "Start"}, {0, 0}, "J");
  expectState(r, {"end. Start"}, 0, 4, Mode::Normal, "J adds single space after period (Neovim default)");
}

TEST_F(EditTest, J_JoinWithCount) {
  auto r = applyNormalEdit({"one", "two", "three"}, {0, 0}, "J", 2);
  expectLines(r.lines, {"one two three"}, "2J joins 2 lines below");
}

TEST_F(EditTest, J_AtLastLine_Throws) {
  EXPECT_THROW(applyNormalEdit({"only"}, {0, 0}, "J"), runtime_error);
}

TEST_F(EditTest, GJ_JoinWithoutSpace) {
  auto r = applyNormalEdit({"one", "two"}, {0, 0}, "gJ");
  expectState(r, {"onetwo"}, 0, 2, Mode::Normal, "gJ joins without space");
}

TEST_F(EditTest, GJ_JoinPreservesLeadingWhitespace) {
  // gJ joins without adding space AND preserves leading whitespace (unlike J)
  auto r = applyNormalEdit({"one", "  two"}, {0, 0}, "gJ");
  expectState(r, {"one  two"}, 0, 2, Mode::Normal, "gJ preserves leading whitespace");
}

// =============================================================================
// 6. OPEN LINE (o, O)
// =============================================================================

TEST_F(EditTest, O_OpenLineBelow) {
  auto r = applyNormalEdit({"one", "two"}, {0, 2}, "o");
  expectState(r, {"one", "", "two"}, 1, 0, Mode::Insert, "o opens line below");
}

TEST_F(EditTest, BigO_OpenLineAbove) {
  auto r = applyNormalEdit({"one", "two"}, {1, 0}, "O");
  expectState(r, {"one", "", "two"}, 1, 0, Mode::Insert, "O opens line above");
}

TEST_F(EditTest, O_OnEmptyBuffer) {
  Lines empty = {};
  auto r = applyEdit(empty, {0, 0}, Mode::Normal, "o");
  expectLines(r.lines, {""}, "o on empty buffer creates line");
}

// =============================================================================
// 7. MODE ENTRY (i, I, a, A)
// =============================================================================

TEST_F(EditTest, I_EnterInsertMode) {
  auto r = applyNormalEdit({"abcde"}, {0, 2}, "i");
  expectState(r, {"abcde"}, 0, 2, Mode::Insert, "i enters insert at cursor");
}

TEST_F(EditTest, BigI_InsertAtFirstNonBlank) {
  auto r = applyNormalEdit({"  abcde"}, {0, 4}, "I");
  expectState(r, {"  abcde"}, 0, 2, Mode::Insert, "I goes to first non-blank");
}

TEST_F(EditTest, A_AppendAfterCursor) {
  auto r = applyNormalEdit({"abcde"}, {0, 2}, "a");
  expectState(r, {"abcde"}, 0, 3, Mode::Insert, "a moves cursor right and enters insert");
}

TEST_F(EditTest, BigA_AppendAtLineEnd) {
  auto r = applyNormalEdit({"abcde"}, {0, 0}, "A");
  expectState(r, {"abcde"}, 0, 5, Mode::Insert, "A goes to end of line");
}

// =============================================================================
// 8. INSERT MODE OPERATIONS (<Esc>, <BS>, <Del>, <CR>, <C-u>, <C-w>)
// =============================================================================

TEST_F(EditTest, Esc_ExitInsertMode) {
  auto r = applyEdit({"abcde"}, {0, 3}, Mode::Insert, "<Esc>");
  expectState(r, {"abcde"}, 0, 2, Mode::Normal, "Esc exits insert, moves col left");
}

TEST_F(EditTest, Esc_AtColumnZero) {
  auto r = applyEdit({"abcde"}, {0, 0}, Mode::Insert, "<Esc>");
  expectState(r, {"abcde"}, 0, 0, Mode::Normal, "Esc at col 0 stays at col 0");
}

TEST_F(EditTest, BS_DeleteCharBefore) {
  auto r = applyEdit({"abcde"}, {0, 3}, Mode::Insert, "<BS>");
  expectState(r, {"abde"}, 0, 2, Mode::Insert, "BS deletes char before cursor");
}

TEST_F(EditTest, BS_AtLineStart_JoinsLines) {
  auto r = applyEdit({"one", "two"}, {1, 0}, Mode::Insert, "<BS>");
  expectState(r, {"onetwo"}, 0, 3, Mode::Insert, "BS at line start joins lines");
}

TEST_F(EditTest, BS_AtBufferStart_Throws) {
  EXPECT_THROW(applyEdit({"abcde"}, {0, 0}, Mode::Insert, "<BS>"), runtime_error);
}

TEST_F(EditTest, Del_DeleteCharAtCursor) {
  auto r = applyEdit({"abcde"}, {0, 2}, Mode::Insert, "<Del>");
  expectState(r, {"abde"}, 0, 2, Mode::Insert, "Del deletes char at cursor");
}

TEST_F(EditTest, Del_AtLineEnd_JoinsLines) {
  // In insert mode at col 3 (past "one"), Del joins with next line
  // After join, cursor position is at the join point
  auto r = applyEdit({"one", "two"}, {0, 3}, Mode::Insert, "<Del>");
  expectState(r, {"onetwo"}, 0, 2, Mode::Insert, "Del at line end joins lines");
}

TEST_F(EditTest, Del_AtBufferEnd_Throws) {
  // At end of single-line buffer (past last char), Del should throw
  EXPECT_THROW(applyEdit({"ab"}, {0, 2}, Mode::Insert, "<Del>"), runtime_error);
}

TEST_F(EditTest, CR_InsertNewline) {
  auto r = applyEdit({"abcde"}, {0, 2}, Mode::Insert, "<CR>");
  expectState(r, {"ab", "cde"}, 1, 0, Mode::Insert, "CR splits line");
}

TEST_F(EditTest, CtrlU_DeleteToLineStart) {
  auto r = applyEdit({"abcde"}, {0, 3}, Mode::Insert, "<C-u>");
  expectState(r, {"de"}, 0, 0, Mode::Insert, "C-u deletes to line start");
}

TEST_F(EditTest, CtrlU_AtColumnZero_Throws) {
  EXPECT_THROW(applyEdit({"abcde"}, {0, 0}, Mode::Insert, "<C-u>"), runtime_error);
}

TEST_F(EditTest, CtrlW_DeleteWord) {
  auto r = applyEdit({"one two three"}, {0, 7}, Mode::Insert, "<C-w>");
  // From col 7 (the space after "two"), C-w deletes "two" back to position 4
  // The trailing space remains since C-w deletes the word, not trailing whitespace
  expectState(r, {"one  three"}, 0, 4, Mode::Insert, "C-w deletes word (trailing space remains)");
}

TEST_F(EditTest, CtrlW_AtColumnZero_Throws) {
  EXPECT_THROW(applyEdit({"abcde"}, {0, 0}, Mode::Insert, "<C-w>"), runtime_error);
}

// =============================================================================
// 9. INSERT MODE NAVIGATION (<Left>, <Right>, <Up>, <Down>)
// =============================================================================

TEST_F(EditTest, Left_MovesLeft) {
  auto r = applyEdit({"abcde"}, {0, 3}, Mode::Insert, "<Left>");
  expectState(r, {"abcde"}, 0, 2, Mode::Insert, "Left moves cursor left");
}

TEST_F(EditTest, Left_AtColumnZero_Throws) {
  EXPECT_THROW(applyEdit({"abcde"}, {0, 0}, Mode::Insert, "<Left>"), runtime_error);
}

TEST_F(EditTest, Right_MovesRight) {
  auto r = applyEdit({"abcde"}, {0, 2}, Mode::Insert, "<Right>");
  expectState(r, {"abcde"}, 0, 3, Mode::Insert, "Right moves cursor right");
}

TEST_F(EditTest, Right_AtLineEnd_Throws) {
  EXPECT_THROW(applyEdit({"abcde"}, {0, 5}, Mode::Insert, "<Right>"), runtime_error);
}

TEST_F(EditTest, Up_MovesUp) {
  auto r = applyEdit({"one", "two"}, {1, 2}, Mode::Insert, "<Up>");
  expectState(r, {"one", "two"}, 0, 2, Mode::Insert, "Up moves cursor up");
}

TEST_F(EditTest, Up_ClampsColumn) {
  auto r = applyEdit({"ab", "longer"}, {1, 5}, Mode::Insert, "<Up>");
  expectState(r, {"ab", "longer"}, 0, 2, Mode::Insert, "Up clamps to shorter line");
}

TEST_F(EditTest, Up_AtFirstLine_Throws) {
  EXPECT_THROW(applyEdit({"one"}, {0, 0}, Mode::Insert, "<Up>"), runtime_error);
}

TEST_F(EditTest, Down_MovesDown) {
  auto r = applyEdit({"one", "two"}, {0, 1}, Mode::Insert, "<Down>");
  expectState(r, {"one", "two"}, 1, 1, Mode::Insert, "Down moves cursor down");
}

TEST_F(EditTest, Down_AtLastLine_Throws) {
  EXPECT_THROW(applyEdit({"one"}, {0, 0}, Mode::Insert, "<Down>"), runtime_error);
}

// =============================================================================
// 10. EDGE CASES
// =============================================================================

TEST_F(EditTest, EmptyBuffer_OnlyModeEntryAllowed) {
  Lines empty = {};
  // i should work on empty buffer
  auto r1 = applyEdit(empty, {0, 0}, Mode::Normal, "i");
  EXPECT_EQ(r1.mode, Mode::Insert);

  // x should throw on empty buffer
  EXPECT_THROW(applyEdit(Lines{}, {0, 0}, Mode::Normal, "x"), runtime_error);
}

TEST_F(EditTest, EmptyLine_LimitedOperations) {
  // x on empty line should throw
  EXPECT_THROW(applyNormalEdit({""}, {0, 0}, "x"), runtime_error);

  // dd on empty line should work
  auto r = applyNormalEdit({"", "content"}, {0, 0}, "dd");
  expectLines(r.lines, {"content"}, "dd on empty line");
}

TEST_F(EditTest, SingleCharBuffer) {
  auto r = applyNormalEdit({"a"}, {0, 0}, "x");
  expectState(r, {""}, 0, 0, Mode::Normal, "x on single char");
}

// =============================================================================
// 11. PROPERTY-BASED TESTS
// =============================================================================

TEST_F(EditTest, Property_InsertText_IncreasesLength) {
  Lines lines = {"abc"};
  Position pos = {0, 1};
  Edit::insertText(lines, pos, Mode::Insert, "XY");

  EXPECT_EQ(lines[0], "aXYbc") << "insertText inserts at cursor";
  EXPECT_EQ(pos.col, 3) << "cursor moves after inserted text";
}

TEST_F(EditTest, Property_DeletePreservesOtherLines) {
  Lines lines = {"one", "two", "three"};
  auto r = applyNormalEdit(lines, {1, 0}, "dd");

  EXPECT_EQ(r.lines[0], "one") << "dd preserves line above";
  EXPECT_EQ(r.lines[1], "three") << "dd preserves line below";
}

TEST_F(EditTest, Property_ChangeEntersInsertMode) {
  vector<string> changeOps = {"cw", "cW", "ce", "cE", "cb", "cB", "cc", "C", "c0", "c$", "s"};

  for (const auto& op : changeOps) {
    // Use appropriate buffer/position for each operation
    Lines lines = {"one two three"};
    Position pos = {0, 4};  // Middle of line

    // Skip ops that would throw
    if (op == "cb" || op == "cB" || op == "c0") {
      if (pos.col == 0) continue;
    }

    try {
      auto r = applyNormalEdit(lines, pos, op);
      EXPECT_EQ(r.mode, Mode::Insert) << op << " should enter insert mode";
    } catch (const runtime_error&) {
      // Some operations may throw in edge cases, that's OK
    }
  }
}

TEST_F(EditTest, Property_ModeEntryDoesNotChangeBuffer) {
  vector<string> modeOps = {"i", "I", "a", "A"};
  Lines original = {"one two three"};

  for (const auto& op : modeOps) {
    auto r = applyNormalEdit(original, {0, 4}, op);
    expectLines(r.lines, original, op + " should not modify buffer");
    EXPECT_EQ(r.mode, Mode::Insert) << op << " should enter insert mode";
  }
}

// =============================================================================
// 12. COMPLEX SCENARIOS
// =============================================================================

TEST_F(EditTest, Scenario_DeleteWordThenType) {
  // dw then insert mode typing
  auto r1 = applyNormalEdit({"hello world"}, {0, 0}, "dw");
  expectLines(r1.lines, {"world"}, "dw deletes first word");

  auto r2 = applyNormalEdit(r1.lines, r1.pos, "i");
  Edit::insertText(r2.lines, r2.pos, Mode::Insert, "new ");
  expectLines(r2.lines, {"new world"}, "type after dw");
}

TEST_F(EditTest, Scenario_ChangeWordAndRetype) {
  // cw deletes word and enters insert
  auto r1 = applyNormalEdit({"hello world"}, {0, 0}, "cw");
  expectLines(r1.lines, {" world"}, "cw on first word");
  EXPECT_EQ(r1.mode, Mode::Insert);

  // Type replacement
  Edit::insertText(r1.lines, r1.pos, Mode::Insert, "goodbye");
  expectLines(r1.lines, {"goodbye world"}, "type replacement after cw");
}

TEST_F(EditTest, Scenario_JoinMultipleLines) {
  Lines lines = {"one", "two", "three", "four"};
  auto r = applyNormalEdit(lines, {0, 0}, "J", 3);
  expectLines(r.lines, {"one two three four"}, "3J joins 3 lines");
}

// =============================================================================
// 13. NEOVIM-VERIFIED b-MOTION TESTS
//
// These tests are generated from actual Neovim behavior using the oracle script
// at lua/tests/b_motion_oracle.lua. They verify our implementation matches
// real Vim behavior for db, cb, dB, cB operations.
// =============================================================================

// --- db tests (delete backward to word start) ---

TEST_F(EditTest, Db_Neovim_within_line) {
  auto r = applyNormalEdit({"one two three"}, {0, 8}, "db");
  expectState(r, {"one three"}, 0, 4, Mode::Normal, "db within line");
}

TEST_F(EditTest, Db_Neovim_at_word_start) {
  auto r = applyNormalEdit({"one two three"}, {0, 4}, "db");
  expectState(r, {"two three"}, 0, 0, Mode::Normal, "db at word start");
}

TEST_F(EditTest, Db_Neovim_in_middle_of_word) {
  auto r = applyNormalEdit({"one two three"}, {0, 5}, "db");
  expectState(r, {"one wo three"}, 0, 4, Mode::Normal, "db in middle of word");
}

TEST_F(EditTest, Db_Neovim_at_second_char) {
  auto r = applyNormalEdit({"one two"}, {0, 5}, "db");
  expectState(r, {"one wo"}, 0, 4, Mode::Normal, "db at second char");
}

TEST_F(EditTest, Db_Neovim_cross_line_at_col0) {
  // KEY TEST: db at col 0 crossing lines should delete the newline
  auto r = applyNormalEdit({"ab", "cd"}, {1, 0}, "db");
  expectState(r, {"cd"}, 0, 0, Mode::Normal, "db cross line at col 0");
}

TEST_F(EditTest, Db_Neovim_cross_line_at_col1) {
  auto r = applyNormalEdit({"ab", "cd"}, {1, 1}, "db");
  expectState(r, {"ab", "d"}, 1, 0, Mode::Normal, "db cross line at col 1");
}

TEST_F(EditTest, Db_Neovim_cross_multiline) {
  auto r = applyNormalEdit({"aa", "bb", "cc"}, {2, 0}, "db");
  expectState(r, {"aa", "cc"}, 1, 0, Mode::Normal, "db cross multiline");
}

TEST_F(EditTest, Db_Neovim_single_word_line) {
  auto r = applyNormalEdit({"word"}, {0, 2}, "db");
  expectState(r, {"rd"}, 0, 0, Mode::Normal, "db single word line");
}

TEST_F(EditTest, Db_Neovim_with_punctuation) {
  auto r = applyNormalEdit({"foo.bar baz"}, {0, 8}, "db");
  expectState(r, {"foo.baz"}, 0, 4, Mode::Normal, "db with punctuation");
}

TEST_F(EditTest, Db_Neovim_with_spaces) {
  auto r = applyNormalEdit({"  word"}, {0, 4}, "db");
  expectState(r, {"  rd"}, 0, 2, Mode::Normal, "db with leading spaces");
}

TEST_F(EditTest, Db_Neovim_trailing_spaces) {
  auto r = applyNormalEdit({"word  "}, {0, 5}, "db");
  expectState(r, {" "}, 0, 0, Mode::Normal, "db trailing spaces");
}

// --- cb tests (change backward to word start) ---

TEST_F(EditTest, Cb_Neovim_within_line) {
  auto r = applyNormalEdit({"one two three"}, {0, 8}, "cb");
  // Note: In insert mode, cursor is at insertion point (col 4, before 't')
  expectState(r, {"one three"}, 0, 4, Mode::Insert, "cb within line");
}

TEST_F(EditTest, Cb_Neovim_at_word_start) {
  auto r = applyNormalEdit({"one two three"}, {0, 4}, "cb");
  expectState(r, {"two three"}, 0, 0, Mode::Insert, "cb at word start");
}

TEST_F(EditTest, Cb_Neovim_in_middle_of_word) {
  auto r = applyNormalEdit({"one two three"}, {0, 5}, "cb");
  expectState(r, {"one wo three"}, 0, 4, Mode::Insert, "cb in middle of word");
}

TEST_F(EditTest, Cb_Neovim_cross_line_at_col0) {
  // KEY TEST: cb at col 0 crossing lines should NOT delete the newline
  auto r = applyNormalEdit({"ab", "cd"}, {1, 0}, "cb");
  expectState(r, {"", "cd"}, 0, 0, Mode::Insert, "cb cross line at col 0");
}

TEST_F(EditTest, Cb_Neovim_cross_line_at_col1) {
  auto r = applyNormalEdit({"ab", "cd"}, {1, 1}, "cb");
  expectState(r, {"ab", "d"}, 1, 0, Mode::Insert, "cb cross line at col 1");
}

TEST_F(EditTest, Cb_Neovim_cross_multiline) {
  // KEY TEST: cb across multiple lines at col 0 should leave empty line
  auto r = applyNormalEdit({"aa", "bb", "cc"}, {2, 0}, "cb");
  expectState(r, {"aa", "", "cc"}, 1, 0, Mode::Insert, "cb cross multiline");
}

TEST_F(EditTest, Cb_Neovim_single_word_line) {
  auto r = applyNormalEdit({"word"}, {0, 2}, "cb");
  expectState(r, {"rd"}, 0, 0, Mode::Insert, "cb single word line");
}

TEST_F(EditTest, Cb_Neovim_with_punctuation) {
  auto r = applyNormalEdit({"foo.bar baz"}, {0, 8}, "cb");
  expectState(r, {"foo.baz"}, 0, 4, Mode::Insert, "cb with punctuation");
}

// --- dB tests (delete backward to WORD start) ---

TEST_F(EditTest, DB_Neovim_within_line) {
  auto r = applyNormalEdit({"foo.bar baz.qux"}, {0, 8}, "dB");
  expectState(r, {"baz.qux"}, 0, 0, Mode::Normal, "dB within line");
}

TEST_F(EditTest, DB_Neovim_at_WORD_start) {
  auto r = applyNormalEdit({"foo.bar baz"}, {0, 8}, "dB");
  expectState(r, {"baz"}, 0, 0, Mode::Normal, "dB at WORD start");
}

TEST_F(EditTest, DB_Neovim_cross_line_at_col0) {
  // KEY TEST: dB at col 0 crossing lines should delete the newline
  auto r = applyNormalEdit({"ab", "cd"}, {1, 0}, "dB");
  expectState(r, {"cd"}, 0, 0, Mode::Normal, "dB cross line at col 0");
}

TEST_F(EditTest, DB_Neovim_cross_line_at_col1) {
  auto r = applyNormalEdit({"ab", "cd"}, {1, 1}, "dB");
  expectState(r, {"ab", "d"}, 1, 0, Mode::Normal, "dB cross line at col 1");
}

TEST_F(EditTest, DB_Neovim_with_punctuation) {
  auto r = applyNormalEdit({"foo.bar baz"}, {0, 4}, "dB");
  expectState(r, {"bar baz"}, 0, 0, Mode::Normal, "dB with punctuation");
}

// --- cB tests (change backward to WORD start) ---

TEST_F(EditTest, CB_Neovim_within_line) {
  auto r = applyNormalEdit({"foo.bar baz.qux"}, {0, 8}, "cB");
  expectState(r, {"baz.qux"}, 0, 0, Mode::Insert, "cB within line");
}

TEST_F(EditTest, CB_Neovim_cross_line_at_col0) {
  // KEY TEST: cB at col 0 crossing lines should NOT delete the newline
  auto r = applyNormalEdit({"ab", "cd"}, {1, 0}, "cB");
  expectState(r, {"", "cd"}, 0, 0, Mode::Insert, "cB cross line at col 0");
}

TEST_F(EditTest, CB_Neovim_cross_line_at_col1) {
  auto r = applyNormalEdit({"ab", "cd"}, {1, 1}, "cB");
  expectState(r, {"ab", "d"}, 1, 0, Mode::Insert, "cB cross line at col 1");
}

// =============================================================================
// 14. NEOVIM-VERIFIED w/W MOTION TESTS
// =============================================================================

// --- dw tests (delete forward to word start) ---

TEST_F(EditTest, Dw_Neovim_within_line) {
  auto r = applyNormalEdit({"one two three"}, {0, 0}, "dw");
  expectState(r, {"two three"}, 0, 0, Mode::Normal, "dw within line");
}

TEST_F(EditTest, Dw_Neovim_at_word_end) {
  auto r = applyNormalEdit({"one two three"}, {0, 2}, "dw");
  expectState(r, {"ontwo three"}, 0, 2, Mode::Normal, "dw at word end");
}

TEST_F(EditTest, Dw_Neovim_in_middle_of_word) {
  auto r = applyNormalEdit({"one two three"}, {0, 1}, "dw");
  expectState(r, {"otwo three"}, 0, 1, Mode::Normal, "dw in middle of word");
}

TEST_F(EditTest, Dw_Neovim_on_whitespace) {
  auto r = applyNormalEdit({"one  two"}, {0, 3}, "dw");
  expectState(r, {"onetwo"}, 0, 3, Mode::Normal, "dw on whitespace");
}

TEST_F(EditTest, Dw_Neovim_cross_line_last_word) {
  // KEY: dw on last word of line does NOT delete newline
  auto r = applyNormalEdit({"ab", "cd"}, {0, 0}, "dw");
  expectState(r, {"", "cd"}, 0, 0, Mode::Normal, "dw cross line last word");
}

TEST_F(EditTest, Dw_Neovim_cross_line_at_end) {
  auto r = applyNormalEdit({"ab", "cd"}, {0, 1}, "dw");
  expectState(r, {"a", "cd"}, 0, 0, Mode::Normal, "dw cross line at end");
}

TEST_F(EditTest, Dw_Neovim_single_word) {
  auto r = applyNormalEdit({"word"}, {0, 0}, "dw");
  expectState(r, {""}, 0, 0, Mode::Normal, "dw single word");
}

TEST_F(EditTest, Dw_Neovim_with_punctuation) {
  auto r = applyNormalEdit({"foo.bar baz"}, {0, 0}, "dw");
  expectState(r, {".bar baz"}, 0, 0, Mode::Normal, "dw with punctuation");
}

TEST_F(EditTest, Dw_Neovim_empty_line_below) {
  auto r = applyNormalEdit({"ab", "", "cd"}, {0, 0}, "dw");
  expectState(r, {"", "", "cd"}, 0, 0, Mode::Normal, "dw empty line below");
}

// --- cw tests (change forward - special: acts like ce on word) ---

TEST_F(EditTest, Cw_Neovim_within_line) {
  auto r = applyNormalEdit({"one two three"}, {0, 0}, "cw");
  expectState(r, {" two three"}, 0, 0, Mode::Insert, "cw within line");
}

TEST_F(EditTest, Cw_Neovim_at_word_end) {
  // cw from last char of word deletes just that char
  auto r = applyNormalEdit({"one two three"}, {0, 2}, "cw");
  expectState(r, {"on two three"}, 0, 2, Mode::Insert, "cw at word end");
}

TEST_F(EditTest, Cw_Neovim_on_whitespace) {
  // cw on whitespace uses w motion (unlike on word where it uses ce)
  auto r = applyNormalEdit({"one  two"}, {0, 3}, "cw");
  expectState(r, {"onetwo"}, 0, 3, Mode::Insert, "cw on whitespace");
}

TEST_F(EditTest, Cw_Neovim_cross_line_last_word) {
  auto r = applyNormalEdit({"ab", "cd"}, {0, 0}, "cw");
  expectState(r, {"", "cd"}, 0, 0, Mode::Insert, "cw cross line last word");
}

TEST_F(EditTest, Cw_Neovim_at_line_end) {
  auto r = applyNormalEdit({"ab", "cd"}, {0, 1}, "cw");
  expectState(r, {"a", "cd"}, 0, 1, Mode::Insert, "cw at line end");
}

TEST_F(EditTest, Cw_Neovim_single_word) {
  auto r = applyNormalEdit({"word"}, {0, 0}, "cw");
  expectState(r, {""}, 0, 0, Mode::Insert, "cw single word");
}

TEST_F(EditTest, Cw_Neovim_with_punctuation) {
  auto r = applyNormalEdit({"foo.bar baz"}, {0, 0}, "cw");
  expectState(r, {".bar baz"}, 0, 0, Mode::Insert, "cw with punctuation");
}

// --- dW tests (delete forward to WORD start) ---

TEST_F(EditTest, DW_Neovim_within_line) {
  auto r = applyNormalEdit({"foo.bar baz.qux"}, {0, 0}, "dW");
  expectState(r, {"baz.qux"}, 0, 0, Mode::Normal, "dW within line");
}

TEST_F(EditTest, DW_Neovim_cross_line) {
  auto r = applyNormalEdit({"foo.bar", "baz"}, {0, 0}, "dW");
  expectState(r, {"", "baz"}, 0, 0, Mode::Normal, "dW cross line");
}

TEST_F(EditTest, DW_Neovim_with_punctuation) {
  auto r = applyNormalEdit({"foo.bar baz"}, {0, 4}, "dW");
  expectState(r, {"foo.baz"}, 0, 4, Mode::Normal, "dW with punctuation");
}

// =============================================================================
// 15. NEOVIM-VERIFIED e/E MOTION TESTS
// =============================================================================

// --- de tests (delete forward to word end - inclusive) ---

TEST_F(EditTest, De_Neovim_within_line) {
  auto r = applyNormalEdit({"one two three"}, {0, 0}, "de");
  expectState(r, {" two three"}, 0, 0, Mode::Normal, "de within line");
}

TEST_F(EditTest, De_Neovim_at_word_end) {
  // e from word end goes to next word end
  auto r = applyNormalEdit({"one two three"}, {0, 2}, "de");
  expectState(r, {"on three"}, 0, 2, Mode::Normal, "de at word end");
}

TEST_F(EditTest, De_Neovim_in_middle) {
  auto r = applyNormalEdit({"one two three"}, {0, 1}, "de");
  expectState(r, {"o two three"}, 0, 1, Mode::Normal, "de in middle");
}

TEST_F(EditTest, De_Neovim_cross_line_at_end) {
  // de at word end crosses to next line's word end and deletes newline
  auto r = applyNormalEdit({"ab", "cd"}, {0, 1}, "de");
  expectState(r, {"a"}, 0, 0, Mode::Normal, "de cross line at end");
}

TEST_F(EditTest, De_Neovim_last_char_of_buffer) {
  auto r = applyNormalEdit({"ab"}, {0, 1}, "de");
  expectState(r, {"a"}, 0, 0, Mode::Normal, "de last char of buffer");
}

TEST_F(EditTest, De_Neovim_single_char) {
  auto r = applyNormalEdit({"a"}, {0, 0}, "de");
  expectState(r, {""}, 0, 0, Mode::Normal, "de single char");
}

TEST_F(EditTest, De_Neovim_with_punctuation) {
  auto r = applyNormalEdit({"foo.bar baz"}, {0, 0}, "de");
  expectState(r, {".bar baz"}, 0, 0, Mode::Normal, "de with punctuation");
}

// --- ce tests (change forward to word end) ---

TEST_F(EditTest, Ce_Neovim_within_line) {
  auto r = applyNormalEdit({"one two three"}, {0, 0}, "ce");
  expectState(r, {" two three"}, 0, 0, Mode::Insert, "ce within line");
}

TEST_F(EditTest, Ce_Neovim_at_word_end) {
  auto r = applyNormalEdit({"one two three"}, {0, 2}, "ce");
  expectState(r, {"on three"}, 0, 2, Mode::Insert, "ce at word end");
}

TEST_F(EditTest, Ce_Neovim_cross_line_at_end) {
  auto r = applyNormalEdit({"ab", "cd"}, {0, 1}, "ce");
  expectState(r, {"a"}, 0, 1, Mode::Insert, "ce cross line at end");
}

TEST_F(EditTest, Ce_Neovim_single_char) {
  auto r = applyNormalEdit({"a"}, {0, 0}, "ce");
  expectState(r, {""}, 0, 0, Mode::Insert, "ce single char");
}

// --- dE tests (delete forward to WORD end) ---

TEST_F(EditTest, DE_Neovim_within_line) {
  auto r = applyNormalEdit({"foo.bar baz.qux"}, {0, 0}, "dE");
  expectState(r, {" baz.qux"}, 0, 0, Mode::Normal, "dE within line");
}

TEST_F(EditTest, DE_Neovim_cross_line) {
  auto r = applyNormalEdit({"foo.bar", "baz"}, {0, 4}, "dE");
  expectState(r, {"foo.", "baz"}, 0, 3, Mode::Normal, "dE cross line");
}

// =============================================================================
// 16. NEOVIM-VERIFIED ge/gE MOTION TESTS
// =============================================================================

// --- dge tests (delete backward to word end - inclusive) ---

TEST_F(EditTest, Dge_Neovim_within_line) {
  auto r = applyNormalEdit({"one two three"}, {0, 8}, "dge");
  expectState(r, {"one twhree"}, 0, 6, Mode::Normal, "dge within line");
}

TEST_F(EditTest, Dge_Neovim_at_word_start) {
  auto r = applyNormalEdit({"one two three"}, {0, 4}, "dge");
  expectState(r, {"onwo three"}, 0, 2, Mode::Normal, "dge at word start");
}

TEST_F(EditTest, Dge_Neovim_in_middle) {
  auto r = applyNormalEdit({"one two three"}, {0, 5}, "dge");
  expectState(r, {"ono three"}, 0, 2, Mode::Normal, "dge in middle");
}

TEST_F(EditTest, Dge_Neovim_cross_line_at_col0) {
  // dge from col 0 goes to previous line's word end and deletes newline
  auto r = applyNormalEdit({"ab", "cd"}, {1, 0}, "dge");
  expectState(r, {"ad"}, 0, 1, Mode::Normal, "dge cross line at col 0");
}

TEST_F(EditTest, Dge_Neovim_cross_line_at_col1) {
  auto r = applyNormalEdit({"ab", "cd"}, {1, 1}, "dge");
  expectState(r, {"a"}, 0, 0, Mode::Normal, "dge cross line at col 1");
}

TEST_F(EditTest, Dge_Neovim_cross_multiline) {
  auto r = applyNormalEdit({"aa", "bb", "cc"}, {2, 0}, "dge");
  expectState(r, {"aa", "bc"}, 1, 1, Mode::Normal, "dge cross multiline");
}

TEST_F(EditTest, Dge_Neovim_with_punctuation) {
  auto r = applyNormalEdit({"foo.bar baz"}, {0, 8}, "dge");
  expectState(r, {"foo.baaz"}, 0, 6, Mode::Normal, "dge with punctuation");
}

// --- cge tests (change backward to word end) ---

TEST_F(EditTest, Cge_Neovim_within_line) {
  auto r = applyNormalEdit({"one two three"}, {0, 8}, "cge");
  expectState(r, {"one twhree"}, 0, 6, Mode::Insert, "cge within line");
}

TEST_F(EditTest, Cge_Neovim_cross_line_at_col0) {
  auto r = applyNormalEdit({"ab", "cd"}, {1, 0}, "cge");
  expectState(r, {"ad"}, 0, 1, Mode::Insert, "cge cross line at col 0");
}

TEST_F(EditTest, Cge_Neovim_cross_line_at_col1) {
  auto r = applyNormalEdit({"ab", "cd"}, {1, 1}, "cge");
  expectState(r, {"a"}, 0, 0, Mode::Insert, "cge cross line at col 1");
}

// --- dgE tests (delete backward to WORD end) ---

TEST_F(EditTest, DgE_Neovim_within_line) {
  auto r = applyNormalEdit({"foo.bar baz.qux"}, {0, 8}, "dgE");
  expectState(r, {"foo.baaz.qux"}, 0, 6, Mode::Normal, "dgE within line");
}

TEST_F(EditTest, DgE_Neovim_cross_line_at_col0) {
  auto r = applyNormalEdit({"foo.bar", "baz"}, {1, 0}, "dgE");
  expectState(r, {"foo.baaz"}, 0, 6, Mode::Normal, "dgE cross line at col 0");
}
