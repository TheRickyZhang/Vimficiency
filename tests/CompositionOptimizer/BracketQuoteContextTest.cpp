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

class TextObjectContextTest : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  Config config = Config::uniform();
  CompositionOptimizerParams params{};

  static void SetUpTestSuite() { oracle = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }

  // Create a CompositionSearchContext from initial → goal.
  // Uses initialPos=(0,0) to ensure forward processing.
  CompositionSearchContext makeContext(const Lines& initial, const Lines& goal) {
    return CompositionSearchContext(
        initial, Position(0, 0), goal, "",
        NavContext(), MotionBoundary(), EXPLORABLE_MOTIONS, params, config);
  }

  // Validate the quote mask for a single edit against oracle.
  // For each column on the edit's line:
  //   mask valid   → ci/a{quote} + insertedText should produce goal
  //   mask invalid → ci/a{quote} + insertedText should NOT produce goal
  void validateQuoteMask(
      const Lines& initial, const Lines& goal,
      const TextObjectContext& toCtx, const DiffState& diff, char quote) {
    if (toCtx.line < 0) return;

    bool isAround = toCtx.useAroundQuote.seen(quote);
    string modifier = isAround ? "a" : "i";
    string cmd = "c" + modifier + string(1, quote) + diff.insertedText + "<Esc>";

    int lineLen = static_cast<int>(initial[toCtx.line].size());
    for (int col = 0; col < lineLen; col++) {
      bool maskValid = col < static_cast<int>(toCtx.validQuoteMask.size())
                       && toCtx.validQuoteMask[col].seen(quote);

      SimulationResult result = oracle->simulate(initial, toCtx.line, col, cmd);
      bool matchesGoal = (result.lines == goal);

      EXPECT_EQ(maskValid, matchesGoal)
          << "Quote '" << quote << "' mismatch at col " << col
          << " on '" << initial[toCtx.line] << "'\n"
          << "  Mask says: " << (maskValid ? "valid" : "invalid") << "\n"
          << "  Command: " << cmd << "\n"
          << "  Oracle output: " << result.lines << "\n"
          << "  Expected goal: " << goal;
    }
  }

  // Validate the bracket mask for a single edit against oracle.
  void validateBracketMask(
      const Lines& initial, const Lines& goal,
      const TextObjectContext& toCtx, const DiffState& diff, char bracket) {
    if (toCtx.line < 0) return;

    bool isAround = toCtx.useAroundBracket.seen(bracket);
    string modifier = isAround ? "a" : "i";
    string cmd = "c" + modifier + string(1, bracket) + diff.insertedText + "<Esc>";

    int lineLen = static_cast<int>(initial[toCtx.line].size());
    for (int col = 0; col < lineLen; col++) {
      bool maskValid = col < static_cast<int>(toCtx.validBracketMask.size())
                       && toCtx.validBracketMask[col].seen(bracket);

      SimulationResult result = oracle->simulate(initial, toCtx.line, col, cmd);
      bool matchesGoal = (result.lines == goal);

      EXPECT_EQ(maskValid, matchesGoal)
          << "Bracket '" << bracket << "' mismatch at col " << col
          << " on '" << initial[toCtx.line] << "'\n"
          << "  Mask says: " << (maskValid ? "valid" : "invalid") << "\n"
          << "  Command: " << cmd << "\n"
          << "  Oracle output: " << result.lines << "\n"
          << "  Expected goal: " << goal;
    }
  }
};

unique_ptr<NeovimOracle> TextObjectContextTest::oracle;

// =============================================================================
// Quote Text Object Context
// =============================================================================

TEST_F(TextObjectContextTest, InnerQuote_SinglePair) {
  Lines initial = {"foo \"hello\" bar"};
  Lines goal = {"foo \"X\" bar"};

  auto ctx = makeContext(initial, goal);
  ASSERT_EQ(ctx.totalEdits, 1);

  const TextObjectContext& toCtx = ctx.textObjectContexts[0];
  EXPECT_FALSE(toCtx.useAroundQuote.seen('"'));
  validateQuoteMask(initial, goal, toCtx, ctx.diffStates[0], '"');
}

TEST_F(TextObjectContextTest, InnerQuote_SingleQuote) {
  Lines initial = {"foo 'hello' bar"};
  Lines goal = {"foo 'X' bar"};

  auto ctx = makeContext(initial, goal);
  ASSERT_EQ(ctx.totalEdits, 1);

  const TextObjectContext& toCtx = ctx.textObjectContexts[0];
  EXPECT_FALSE(toCtx.useAroundQuote.seen('\''));
  validateQuoteMask(initial, goal, toCtx, ctx.diffStates[0], '\'');
}

TEST_F(TextObjectContextTest, InnerQuote_Backtick) {
  Lines initial = {"foo `hello` bar"};
  Lines goal = {"foo `X` bar"};

  auto ctx = makeContext(initial, goal);
  ASSERT_EQ(ctx.totalEdits, 1);

  const TextObjectContext& toCtx = ctx.textObjectContexts[0];
  EXPECT_FALSE(toCtx.useAroundQuote.seen('`'));
  validateQuoteMask(initial, goal, toCtx, ctx.diffStates[0], '`');
}

TEST_F(TextObjectContextTest, InnerQuote_SecondPair) {
  // Edit targets the second " pair - positions before first " should NOT be valid
  // because ci" from there would change "first" instead
  Lines initial = {"aaa \"first\" bbb \"second\" ccc"};
  Lines goal = {"aaa \"first\" bbb \"X\" ccc"};

  auto ctx = makeContext(initial, goal);
  ASSERT_EQ(ctx.totalEdits, 1);

  const TextObjectContext& toCtx = ctx.textObjectContexts[0];
  EXPECT_FALSE(toCtx.useAroundQuote.seen('"'));
  validateQuoteMask(initial, goal, toCtx, ctx.diffStates[0], '"');
}

// =============================================================================
// Bracket Text Object Context
// =============================================================================

TEST_F(TextObjectContextTest, InnerParen_SinglePair) {
  Lines initial = {"foo (hello) bar"};
  Lines goal = {"foo (X) bar"};

  auto ctx = makeContext(initial, goal);
  ASSERT_EQ(ctx.totalEdits, 1);

  const TextObjectContext& toCtx = ctx.textObjectContexts[0];
  EXPECT_FALSE(toCtx.useAroundBracket.seen('('));
  validateBracketMask(initial, goal, toCtx, ctx.diffStates[0], '(');
}

TEST_F(TextObjectContextTest, InnerBrace_SinglePair) {
  Lines initial = {"foo {hello} bar"};
  Lines goal = {"foo {X} bar"};

  auto ctx = makeContext(initial, goal);
  ASSERT_EQ(ctx.totalEdits, 1);

  const TextObjectContext& toCtx = ctx.textObjectContexts[0];
  EXPECT_FALSE(toCtx.useAroundBracket.seen('{'));
  validateBracketMask(initial, goal, toCtx, ctx.diffStates[0], '{');
}

TEST_F(TextObjectContextTest, InnerBracket_SquareBracket) {
  Lines initial = {"foo [hello] bar"};
  Lines goal = {"foo [X] bar"};

  auto ctx = makeContext(initial, goal);
  ASSERT_EQ(ctx.totalEdits, 1);

  const TextObjectContext& toCtx = ctx.textObjectContexts[0];
  EXPECT_FALSE(toCtx.useAroundBracket.seen('['));
  validateBracketMask(initial, goal, toCtx, ctx.diffStates[0], '[');
}

TEST_F(TextObjectContextTest, InnerBracket_Nested) {
  // Edit targets inner pair content - which columns allow ci( to reach it?
  Lines initial = {"a ((hello)) c"};
  Lines goal = {"a ((X)) c"};

  auto ctx = makeContext(initial, goal);
  ASSERT_EQ(ctx.totalEdits, 1);

  const TextObjectContext& toCtx = ctx.textObjectContexts[0];
  EXPECT_FALSE(toCtx.useAroundBracket.seen('('));
  validateBracketMask(initial, goal, toCtx, ctx.diffStates[0], '(');
}

TEST_F(TextObjectContextTest, InnerBracket_NestedEditOuter) {
  // Edit targets outer pair content - different valid positions than inner
  Lines initial = {"a ((b)) c"};
  Lines goal = {"a (X) c"};

  auto ctx = makeContext(initial, goal);
  ASSERT_EQ(ctx.totalEdits, 1);

  const TextObjectContext& toCtx = ctx.textObjectContexts[0];
  EXPECT_FALSE(toCtx.useAroundBracket.seen('('));
  validateBracketMask(initial, goal, toCtx, ctx.diffStates[0], '(');
}

// =============================================================================
// No Valid Text Objects
// =============================================================================

TEST_F(TextObjectContextTest, NoDelimiters) {
  // No quotes or brackets - TextObjectContext should have no valid entries
  Lines initial = {"hello world"};
  Lines goal = {"hello there"};

  auto ctx = makeContext(initial, goal);
  ASSERT_EQ(ctx.totalEdits, 1);

  const TextObjectContext& toCtx = ctx.textObjectContexts[0];
  EXPECT_FALSE(toCtx.hasAnyValid());
}

TEST_F(TextObjectContextTest, PureInsertion_SkipsContext) {
  // Pure insertions have no TextObjectContext (nothing to match against)
  Lines initial = {"hello"};
  Lines goal = {"hello world"};

  auto ctx = makeContext(initial, goal);
  ASSERT_EQ(ctx.totalEdits, 1);

  const TextObjectContext& toCtx = ctx.textObjectContexts[0];
  EXPECT_EQ(toCtx.line, -1);
  EXPECT_FALSE(toCtx.hasAnyValid());
}

// =============================================================================
// Randomized Correctness Tests
// =============================================================================
// Generates fully random lines containing arbitrary mixes of quotes, brackets,
// and filler text. Picks a random contiguous region to edit, computes
// TextObjectContext via CompositionSearchContext, and validates every column's
// mask entry against the Neovim oracle for all delimiter types present.

namespace {

// All delimiter chars that can appear in generated lines
constexpr string_view DELIMITERS = "\"'`()[]{}";
constexpr string_view FILLER = "abcdefg ";

// Generate a random line with arbitrary delimiter structure.
// Uses weighted random: mostly filler, with a meaningful chance of delimiters.
string randomDelimiterLine(int len) {
  string line;
  line.reserve(len);
  for (int i = 0; i < len; i++) {
    line += RandomGen::pick<string_view>({
        {70, FILLER},
        {30, DELIMITERS},
    });
  }
  return line;
}

// Pick a random contiguous edit region [begin, end) within a line,
// then replace it with different random filler to produce the goal.
// Returns false if no valid single-edit diff could be produced.
bool randomEdit(const string& line, string& goalLine) {
  int len = static_cast<int>(line.size());
  if (len < 2) return false;

  // Pick two distinct positions to form [begin, end)
  int a = RandomGen::range(0, len - 1);
  int b = RandomGen::range(0, len - 1);
  if (a == b) return false;
  int begin = min(a, b);
  int end = max(a, b);

  // Generate replacement text (different length adds variety)
  int newLen = RandomGen::range(1, 6);
  string replacement;
  for (int i = 0; i < newLen; i++) {
    replacement += RandomGen::pick(FILLER);
  }

  // Build goal line
  goalLine = line.substr(0, begin) + replacement + line.substr(end);

  // Ensure it actually differs
  return goalLine != line;
}

// Collect which quote chars appear in the line
vector<char> quotesInLine(const string& line) {
  vector<char> result;
  for (char q : {'\"', '\'', '`'}) {
    if (line.find(q) != string::npos) result.push_back(q);
  }
  return result;
}

// Collect which opening bracket chars appear in the line
vector<char> bracketsInLine(const string& line) {
  vector<char> result;
  for (char b : {'(', '[', '{'}) {
    if (line.find(b) != string::npos) result.push_back(b);
  }
  return result;
}

} // namespace

TEST_F(TextObjectContextTest, Random_FullyRandom) {
  const int NUM_ITERATIONS = 20;
  RandomGen::seed(42);
  int validated = 0;

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    // Generate random line with delimiters
    int lineLen = RandomGen::range(8, 30);
    string line = randomDelimiterLine(lineLen);

    // Generate random edit
    string goalLine;
    if (!randomEdit(line, goalLine)) continue;

    Lines initial = {line};
    Lines goal = {goalLine};

    auto ctx = makeContext(initial, goal);
    if (ctx.totalEdits < 1) continue;

    // Compute intermediate states: states[i] is buffer before edit i,
    // states[i+1] is buffer after edit i.
    vector<Lines> states(ctx.totalEdits + 1);
    states[0] = initial;
    for (int i = 0; i < ctx.totalEdits; i++) {
      states[i + 1] = Myers::applyDiffState(ctx.diffStates[i], states[i]);
    }

    for (int e = 0; e < ctx.totalEdits; e++) {
      const auto& toCtx = ctx.textObjectContexts[e];
      if (toCtx.line < 0) continue;  // pure insertion - no mask to validate

      const Lines& editInitial = states[e];
      const Lines& editGoal = states[e + 1];
      const string& editLine = editInitial[toCtx.line];

      validated++;

      // Validate ALL quote types present on the edit's line
      for (char q : quotesInLine(editLine)) {
        validateQuoteMask(editInitial, editGoal, toCtx, ctx.diffStates[e], q);
      }

      // Validate ALL bracket types present on the edit's line
      for (char b : bracketsInLine(editLine)) {
        validateBracketMask(editInitial, editGoal, toCtx, ctx.diffStates[e], b);
      }
    }
  }

  EXPECT_GT(validated, NUM_ITERATIONS / 2) << "Too few valid test cases generated";
}
