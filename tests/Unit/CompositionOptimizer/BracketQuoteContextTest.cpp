// tests/Unit/CompositionOptimizer/BracketQuoteContextTest.cpp
//
// Validates TextObjectContext masks: for each column on an edit's line,
// the mask should agree with Neovim on whether ci"/ca"/ci(/ca( etc.
// from that column produces the exact edit region.
//
// Run: ./build/tests/vimfy_unit_tests --gtest_filter="TextObjectContextTest.*"

#include <gtest/gtest.h>
#include <memory>

#include "Boundary/NavBoundary.h"
#include "Types/NavContext.h"
#include "Optimizer/CompositionOptimizer/CompositionSearchContext.h"
#include "Keyboard/Config.h"
#include "Types/Lines.h"
#include "Utils/NeovimOracle.h"

using namespace std;

namespace {
bool isQuoteChar(char c) { return c == '"' || c == '\'' || c == '`'; }
} // namespace

class TextObjectContextTest : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  Config config = Config::uniform();
  CompositionOptimizerParams params{};

  static void SetUpTestSuite() { oracle = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }

  CompositionSearchContext makeContext(const Lines& initial, const Lines& goal) {
    return CompositionSearchContext(
        initial, CursorPos(0, 0), goal, CursorPos(0, 0), "",
        NavContext(), NavBoundary(), params, config);
  }

  // Validate mask for any delimiter type (quote or bracket) against oracle.
  void validateMask(
      const Lines& initial, const Lines& goal,
      const BracketQuoteContext& toCtx, const DiffState& diff, char delim) {
    if (toCtx.line < 0) return;

    bool quote = isQuoteChar(delim);
    bool around = quote ? toCtx.useAroundQuote.seen(delim)
                        : toCtx.useAroundBracket.seen(delim);
    string cmd = "c"s + (around ? "a" : "i") + delim + diff.insertedText + "<Esc>";

    int lineLen = static_cast<int>(initial[toCtx.line].size());
    for (int col = 0; col < lineLen; col++) {
      bool maskValid = quote
          ? (col < static_cast<int>(toCtx.validQuoteMask.size())
             && toCtx.validQuoteMask[col].seen(delim))
          : (col < static_cast<int>(toCtx.validBracketMask.size())
             && toCtx.validBracketMask[col].seen(delim));

      auto result = oracle->simulate(initial, toCtx.line, col, cmd);
      EXPECT_EQ(maskValid, result.lines == goal)
          << "'" << delim << "' mismatch at col " << col
          << " on '" << initial[toCtx.line] << "'\n"
          << "  Mask: " << (maskValid ? "valid" : "invalid")
          << "  Cmd: " << cmd << "\n"
          << "  Oracle: " << result.lines << "\n"
          << "  Goal:   " << goal;
    }
  }

  // Convenience: make context and validate a single-edit case
  void validateSingleEdit(const Lines& initial, const Lines& goal, char delim) {
    auto ctx = makeContext(initial, goal);
    ASSERT_EQ(ctx.totalEdits(), 1);
    validateMask(initial, goal, ctx.edits[0].bracketQuoteContext, ctx.edits[0].diffState, delim);
  }
};

unique_ptr<NeovimOracle> TextObjectContextTest::oracle;

// =============================================================================
// Manual Edge Case Tests
// =============================================================================

// Inner quote: basic single pair
TEST_F(TextObjectContextTest, InnerQuote_SinglePair) {
  validateSingleEdit({"foo \"hello\" bar"}, {"foo \"X\" bar"}, '"');
}

// Second quote pair: positions before first pair must NOT be valid
TEST_F(TextObjectContextTest, InnerQuote_SecondPair) {
  validateSingleEdit(
      {"aaa \"first\" bbb \"second\" ccc"},
      {"aaa \"first\" bbb \"X\" ccc"}, '"');
}

// Inner bracket: basic single pair
TEST_F(TextObjectContextTest, InnerBracket_SinglePair) {
  validateSingleEdit({"foo (hello) bar"}, {"foo (X) bar"}, '(');
}

// Nested brackets: edit targets innermost pair
TEST_F(TextObjectContextTest, InnerBracket_Nested) {
  validateSingleEdit({"a ((hello)) c"}, {"a ((X)) c"}, '(');
}

// Nested level-collapse (((b)) -> (X)): known to fragment at diffOpenPenalty=1.
// The recurse splits the "((" / "))" word seam (keep one paren, delete the
// other), so it is two regions rather than a single ci( edit, and merge cannot
// rejoin them (kept ")" between). The deferred char-granular region-edge fix
// would restore the single edit. See dev/optimizer/diff-generation.md.
TEST_F(TextObjectContextTest, InnerBracket_NestedEditOuter) {
  auto ctx = makeContext({"a ((b)) c"}, {"a (X) c"});
  EXPECT_EQ(ctx.totalEdits(), 2);
}

// No delimiters present: nothing should be valid
TEST_F(TextObjectContextTest, NoDelimiters) {
  auto ctx = makeContext({"hello world"}, {"hello there"});
  ASSERT_EQ(ctx.totalEdits(), 1);
  EXPECT_FALSE(ctx.edits[0].bracketQuoteContext.hasAnyValid());
}

// Pure insertion: context should be skipped (line == -1)
TEST_F(TextObjectContextTest, PureInsertion_SkipsContext) {
  auto ctx = makeContext({"hello"}, {"hello world"});
  ASSERT_EQ(ctx.totalEdits(), 1);
  EXPECT_EQ(ctx.edits[0].bracketQuoteContext.line, -1);
}
