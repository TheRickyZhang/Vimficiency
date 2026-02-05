// tests/CompositionOptimizer/BracketQuoteContextTest.cpp
//
// Validates TextObjectContext masks: for each column on an edit's line,
// the mask should agree with Neovim on whether ci"/ca"/ci(/ca( etc.
// from that column produces the exact edit region.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="TextObjectContextTest.*"

#include <gtest/gtest.h>
#include <memory>

#include "Boundary/MotionBoundary.h"
#include "Editor/NavContext.h"
#include "Keyboard/MotionToKeys.h"
#include "Optimizer/CompositionOptimizer/CompositionSearchContext.h"
#include "Optimizer/Config.h"
#include "Utils/Lines.h"
#include "Utils/NeovimOracle.h"
#include "Utils/RandomGeneration.h"

using namespace std;

namespace {
constexpr string_view ALL_DELIMITERS = "\"'`([{";
constexpr string_view FILLER_CHARS = "abcdefg ";

bool isQuoteChar(char c) { return c == '"' || c == '\'' || c == '`'; }

vector<char> delimsInLine(const string& line) {
  vector<char> result;
  for (char d : ALL_DELIMITERS) {
    if (line.find(d) != string::npos) result.push_back(d);
  }
  return result;
}
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
        initial, Position(0, 0), goal, "",
        NavContext(), MotionBoundary(), EXPLORABLE_MOTIONS, params, config);
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
    ASSERT_EQ(ctx.totalEdits, 1);
    validateMask(initial, goal, ctx.bracketQuoteContexts[0], ctx.diffStates[0], delim);
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

// Nested brackets: edit targets outer pair content
TEST_F(TextObjectContextTest, InnerBracket_NestedEditOuter) {
  validateSingleEdit({"a ((b)) c"}, {"a (X) c"}, '(');
}

// No delimiters present: nothing should be valid
TEST_F(TextObjectContextTest, NoDelimiters) {
  auto ctx = makeContext({"hello world"}, {"hello there"});
  ASSERT_EQ(ctx.totalEdits, 1);
  EXPECT_FALSE(ctx.bracketQuoteContexts[0].hasAnyValid());
}

// Pure insertion: context should be skipped (line == -1)
TEST_F(TextObjectContextTest, PureInsertion_SkipsContext) {
  auto ctx = makeContext({"hello"}, {"hello world"});
  ASSERT_EQ(ctx.totalEdits, 1);
  EXPECT_EQ(ctx.bracketQuoteContexts[0].line, -1);
}

// =============================================================================
// Randomized Correctness Test
// =============================================================================

namespace {

constexpr string_view ALL_CHARS = "\"'`()[]{}";

string randomDelimiterLine(int len) {
  string line;
  line.reserve(len);
  for (int i = 0; i < len; i++) {
    line += RandomGen::pick<string_view>({
        {70, FILLER_CHARS},
        {30, ALL_CHARS},
    });
  }
  return line;
}

bool randomEdit(const string& line, string& goalLine) {
  int len = static_cast<int>(line.size());
  if (len < 2) return false;

  int a = RandomGen::range(0, len - 1);
  int b = RandomGen::range(0, len - 1);
  if (a == b) return false;

  int begin = min(a, b), end = max(a, b);
  string replacement;
  for (int i = 0, n = RandomGen::range(1, 6); i < n; i++)
    replacement += RandomGen::pick(FILLER_CHARS);

  goalLine = line.substr(0, begin) + replacement + line.substr(end);
  return goalLine != line;
}

} // namespace

TEST_F(TextObjectContextTest, Random_FullyRandom) {
  const int NUM_ITERATIONS = 20;
  RandomGen::seed(42);
  int validated = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    string line = randomDelimiterLine(RandomGen::range(8, 30));

    string goalLine;
    if (!randomEdit(line, goalLine)) continue;

    Lines initial = {line};
    Lines goal = {goalLine};

    auto ctx = makeContext(initial, goal);
    if (ctx.totalEdits < 1) continue;

    // Compute intermediate states for multi-edit validation
    vector<Lines> states(ctx.totalEdits + 1);
    states[0] = initial;
    for (int i = 0; i < ctx.totalEdits; i++)
      states[i + 1] = Myers::applyDiffState(ctx.diffStates[i], states[i]);

    for (int e = 0; e < ctx.totalEdits; e++) {
      const auto& toCtx = ctx.bracketQuoteContexts[e];
      if (toCtx.line < 0) continue;

      const Lines& editInitial = states[e];
      const Lines& editGoal = states[e + 1];

      validated++;
      for (char d : delimsInLine(editInitial[toCtx.line]))
        validateMask(editInitial, editGoal, toCtx, ctx.diffStates[e], d);
    }
  }

  EXPECT_GT(validated, NUM_ITERATIONS / 2) << "Too few valid test cases generated";
}
