// tests/CompositionOptimizer/TextObjectContextTest.cpp
//
// Validates that TextObjectContextTest correctly detects when quote/bracket
// text objects (ci", ca", ci(, ca(, etc.) produce the exact edit region needed.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="TextObjectContextTest.*"

#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "Utils/NeovimOracle.h"
#include "Utils/Lines.h"

using namespace std;

class TextObjectContextTest : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;

  static void SetUpTestSuite() { oracle = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }
};

unique_ptr<NeovimOracle> TextObjectContextTest::oracle;

// =============================================================================
// Quote Text Object Tests
// =============================================================================

TEST_F(TextObjectContextTest, QuoteDetection_Manual) {
  // Manual test cases validating quote text object behavior
  // Format: {line, beginCol, endCol, quote, expectAround, description}
  struct TestCase {
    string line;
    int beginCol;
    int endCol;
    char quote;
    bool expectAround;
    string description;
  };

  vector<TestCase> testCases = {
    // Inner quote tests - edit region is content between quotes
    {"\"hello\"", 1, 6, '"', false, "simple inner double quote"},
    {"'hello'", 1, 6, '\'', false, "simple inner single quote"},
    {"`hello`", 1, 6, '`', false, "simple inner backtick"},
    {"foo \"bar\" baz", 5, 8, '"', false, "inner quote mid-line"},
    {"x\"ab\"y", 2, 4, '"', false, "short inner quote"},

    // Around quote tests - edit region includes quotes AND trailing whitespace
    {"\"hello\"", 0, 7, '"', true, "simple around double quote"},
    {"'hello'", 0, 7, '\'', true, "simple around single quote"},
    {"foo \"bar\" baz", 4, 10, '"', true, "around quote mid-line"},  // ca" includes trailing space
  };

  for (const auto& tc : testCases) {
    string modifier = tc.expectAround ? "a" : "i";
    string cmd = "c" + modifier + string(1, tc.quote) + "X<Esc>";

    SimulationResult result = oracle->simulate({tc.line}, 0, 0, cmd);

    // Expected: replace [beginCol, endCol) with "X"
    string expected = tc.line.substr(0, tc.beginCol) + "X" + tc.line.substr(tc.endCol);

    EXPECT_EQ(result.lines.size(), 1) << tc.description;
    EXPECT_EQ(result.lines[0], expected)
        << tc.description << "\n"
        << "  Line: '" << tc.line << "'\n"
        << "  Command: " << cmd << "\n"
        << "  Expected region: [" << tc.beginCol << ", " << tc.endCol << ")\n"
        << "  Expected: '" << expected << "'\n"
        << "  Got: '" << result.lines[0] << "'";
  }
}

TEST_F(TextObjectContextTest, QuoteDetection_Random) {
  // Random test cases for quote text objects
  srand(42);
  const int NUM_ITERATIONS = 50;

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    if (i % 10 == 0) oracle->restart();
    // Generate random content (no quotes)
    int contentLen = 3 + rand() % 8;
    string content;
    for (int j = 0; j < contentLen; j++) {
      content += 'a' + rand() % 26;
    }

    // Generate random prefix/suffix (no quotes)
    int prefixLen = rand() % 5;
    int suffixLen = rand() % 5;
    string prefix, suffix;
    for (int j = 0; j < prefixLen; j++) prefix += 'a' + rand() % 26;
    for (int j = 0; j < suffixLen; j++) suffix += 'a' + rand() % 26;

    // Pick a quote type
    char quote = "\"'`"[rand() % 3];

    // Build line: prefix + quote + content + quote + suffix
    string line = prefix + quote + content + quote + suffix;

    // Test inner quote: ci<quote> replaces content, preserves quotes
    {
      string cmd = string("ci") + quote + "X<Esc>";
      SimulationResult result = oracle->simulate({line}, 0, 0, cmd);
      string expected = prefix + quote + "X" + quote + suffix;

      EXPECT_EQ(result.lines.size(), 1) << "iter=" << i << " inner";
      EXPECT_EQ(result.lines[0], expected)
          << "iter=" << i << " inner\n"
          << "  Line: '" << line << "'\n"
          << "  Expected: '" << expected << "'\n"
          << "  Got: '" << result.lines[0] << "'";
    }

    // Test around quote: ca<quote> replaces quotes and content
    {
      string cmd = string("ca") + quote + "X<Esc>";
      SimulationResult result = oracle->simulate({line}, 0, 0, cmd);
      string expected = prefix + "X" + suffix;

      EXPECT_EQ(result.lines.size(), 1) << "iter=" << i << " around";
      EXPECT_EQ(result.lines[0], expected)
          << "iter=" << i << " around\n"
          << "  Line: '" << line << "'\n"
          << "  Expected: '" << expected << "'\n"
          << "  Got: '" << result.lines[0] << "'";
    }
  }
}

// =============================================================================
// Bracket Text Object Tests
// =============================================================================

TEST_F(TextObjectContextTest, BracketDetection_Manual) {
  oracle->restart();

  struct TestCase {
    string line;
    int beginCol;
    int endCol;
    char bracket;
    bool expectAround;
    string description;
  };

  vector<TestCase> testCases = {
    // Inner bracket tests
    {"(hello)", 1, 6, '(', false, "simple inner paren"},
    {"[hello]", 1, 6, '[', false, "simple inner square"},
    {"{hello}", 1, 6, '{', false, "simple inner brace"},
    {"<hello>", 1, 6, '<', false, "simple inner angle"},
    {"foo (bar) baz", 5, 8, '(', false, "inner paren mid-line"},
    {"foo <bar> baz", 5, 8, '<', false, "inner angle mid-line"},

    // Around bracket tests (brackets do NOT include trailing space like quotes)
    {"(hello)", 0, 7, '(', true, "simple around paren"},
    {"[hello]", 0, 7, '[', true, "simple around square"},
    {"{hello}", 0, 7, '{', true, "simple around brace"},
    {"<hello>", 0, 7, '<', true, "simple around angle"},
    {"foo (bar) baz", 4, 9, '(', true, "around paren mid-line"},  // ca( does NOT include trailing space
    {"foo <bar> baz", 4, 9, '<', true, "around angle mid-line"},
  };

  for (const auto& tc : testCases) {
    oracle->restart();  // Reset for each test case to avoid timeouts

    string modifier = tc.expectAround ? "a" : "i";
    string cmd = "c" + modifier + string(1, tc.bracket) + "X<Esc>";

    SimulationResult result = oracle->simulate({tc.line}, 0, 0, cmd);

    string expected = tc.line.substr(0, tc.beginCol) + "X" + tc.line.substr(tc.endCol);

    EXPECT_EQ(result.lines.size(), 1) << tc.description;
    EXPECT_EQ(result.lines[0], expected)
        << tc.description << "\n"
        << "  Line: '" << tc.line << "'\n"
        << "  Command: " << cmd << "\n"
        << "  Expected region: [" << tc.beginCol << ", " << tc.endCol << ")\n"
        << "  Expected: '" << expected << "'\n"
        << "  Got: '" << result.lines[0] << "'";
  }
}

TEST_F(TextObjectContextTest, BracketDetection_Random) {
  oracle->restart();
  srand(43);
  const int NUM_ITERATIONS = 50;

  // All bracket types including angle brackets
  vector<pair<char, char>> brackets = {{'(', ')'}, {'[', ']'}, {'{', '}'}, {'<', '>'}};

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    if (i % 10 == 0) oracle->restart();
    // Generate random content (no brackets)
    int contentLen = 3 + rand() % 8;
    string content;
    for (int j = 0; j < contentLen; j++) {
      content += 'a' + rand() % 26;
    }

    // Generate random prefix/suffix (no brackets)
    int prefixLen = rand() % 5;
    int suffixLen = rand() % 5;
    string prefix, suffix;
    for (int j = 0; j < prefixLen; j++) prefix += 'a' + rand() % 26;
    for (int j = 0; j < suffixLen; j++) suffix += 'a' + rand() % 26;

    auto [open, close] = brackets[rand() % brackets.size()];

    string line = prefix + open + content + close + suffix;

    // Test inner bracket
    {
      string cmd = string("ci") + open + "X<Esc>";
      SimulationResult result = oracle->simulate({line}, 0, 0, cmd);
      string expected = prefix + open + "X" + close + suffix;

      EXPECT_EQ(result.lines.size(), 1) << "iter=" << i << " inner";
      EXPECT_EQ(result.lines[0], expected)
          << "iter=" << i << " inner ci" << open << "\n"
          << "  Line: '" << line << "'\n"
          << "  Expected: '" << expected << "'\n"
          << "  Got: '" << result.lines[0] << "'";
    }

    // Test around bracket
    {
      string cmd = string("ca") + open + "X<Esc>";
      SimulationResult result = oracle->simulate({line}, 0, 0, cmd);
      string expected = prefix + "X" + suffix;

      EXPECT_EQ(result.lines.size(), 1) << "iter=" << i << " around";
      EXPECT_EQ(result.lines[0], expected)
          << "iter=" << i << " around ca" << open << "\n"
          << "  Line: '" << line << "'\n"
          << "  Expected: '" << expected << "'\n"
          << "  Got: '" << result.lines[0] << "'";
    }
  }
}

// =============================================================================
// Cursor Position Tests - validate text objects work from various positions
// =============================================================================

TEST_F(TextObjectContextTest, QuoteFromDifferentPositions) {
  oracle->restart();

  // ci" should work from any position before or on the closing quote
  string line = "foo \"hello\" bar";
  string expected = "foo \"X\" bar";

  // Valid positions: 0-10 (on or before closing quote at col 10)
  for (int col = 0; col <= 10; col++) {
    string cmd = "ci\"X<Esc>";
    SimulationResult result = oracle->simulate({line}, 0, col, cmd);

    EXPECT_EQ(result.lines.size(), 1) << "col=" << col;
    EXPECT_EQ(result.lines[0], expected)
        << "ci\" from col=" << col << " on '" << line << "'\n"
        << "  Expected: '" << expected << "'\n"
        << "  Got: '" << result.lines[0] << "'";
  }
}

TEST_F(TextObjectContextTest, BracketFromDifferentPositions) {
  oracle->restart();

  // ci( should work from positions where balance is 0 and before closing paren
  string line = "foo (hello) bar";
  string expected = "foo (X) bar";

  // Valid positions: 0-10 (before or on closing paren)
  for (int col = 0; col <= 10; col++) {
    string cmd = "ci(X<Esc>";
    SimulationResult result = oracle->simulate({line}, 0, col, cmd);

    EXPECT_EQ(result.lines.size(), 1) << "col=" << col;
    EXPECT_EQ(result.lines[0], expected)
        << "ci( from col=" << col << " on '" << line << "'\n"
        << "  Expected: '" << expected << "'\n"
        << "  Got: '" << result.lines[0] << "'";
  }
}

TEST_F(TextObjectContextTest, NestedBracketsFromDifferentPositions) {
  oracle->restart();

  // With nested brackets, ci( behavior depends on position
  string line = "a ((b)) c";
  // Outer parens at 2,6; inner at 3,5

  // From col 0-2: ci( uses outer pair (first ( found going forward)
  for (int col = 0; col <= 2; col++) {
    string cmd = "ci(X<Esc>";
    SimulationResult result = oracle->simulate({line}, 0, col, cmd);
    string expected = "a (X) c";  // Outer pair

    EXPECT_EQ(result.lines[0], expected)
        << "ci( from col=" << col << " on '" << line << "' should use outer pair";
  }

  // From col 3 (on inner open paren): ci( uses inner pair
  {
    string cmd = "ci(X<Esc>";
    SimulationResult result = oracle->simulate({line}, 0, 3, cmd);
    string expected = "a ((X)) c";  // Inner pair

    EXPECT_EQ(result.lines[0], expected)
        << "ci( from col=3 on '" << line << "' should use inner pair";
  }
}

TEST_F(TextObjectContextTest, MultipleQuotePairs) {
  oracle->restart();

  // With multiple quote pairs, ci" uses the pair containing cursor or first forward
  // Test with clear separation between pairs
  string line = "aaa \"first\" bbb \"second\" ccc";
  // First pair: quotes at 4 and 10 (content "first" at 5-9)
  // Second pair: quotes at 16 and 23 (content "second" at 17-22)

  // From col 0: uses first pair (searches forward)
  {
    SimulationResult result = oracle->simulate({line}, 0, 0, "ci\"X<Esc>");
    EXPECT_EQ(result.lines[0], "aaa \"X\" bbb \"second\" ccc")
        << "ci\" from col 0 should use first pair";
  }

  oracle->restart();

  // From col 6 (inside first pair, on 'i'): uses first pair
  {
    SimulationResult result = oracle->simulate({line}, 0, 6, "ci\"X<Esc>");
    EXPECT_EQ(result.lines[0], "aaa \"X\" bbb \"second\" ccc")
        << "ci\" from col 6 (inside first pair) should use first pair";
  }

  oracle->restart();

  // From col 17 (inside second pair, on 's'): uses second pair
  {
    SimulationResult result = oracle->simulate({line}, 0, 17, "ci\"X<Esc>");
    EXPECT_EQ(result.lines[0], "aaa \"first\" bbb \"X\" ccc")
        << "ci\" from col 17 (inside second pair) should use second pair";
  }
}
