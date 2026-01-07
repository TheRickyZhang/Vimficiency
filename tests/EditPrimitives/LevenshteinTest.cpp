#include <gtest/gtest.h>

#include "Utils/Lines.h"
#include "Optimizer/Levenshtein.h"

// -----------------------------------------------------------------------------
// Basic Distance Tests
// -----------------------------------------------------------------------------

TEST(LevenshteinTest, IdenticalStringsHaveZeroDistance) {
  Levenshtein lev("hello");
  EXPECT_EQ(lev.distance("hello"), 0);
}

TEST(LevenshteinTest, EmptyGoalReturnsSourceLength) {
  Levenshtein lev("");
  EXPECT_EQ(lev.distance("hello"), 5);
  EXPECT_EQ(lev.distance(""), 0);
}

TEST(LevenshteinTest, EmptySourceReturnsGoalLength) {
  Levenshtein lev("hello");
  EXPECT_EQ(lev.distance(""), 5);
}

TEST(LevenshteinTest, SingleSubstitution) {
  Levenshtein lev("hello");
  EXPECT_EQ(lev.distance("hallo"), 1);  // e -> a
  EXPECT_EQ(lev.distance("jello"), 1);  // h -> j
  EXPECT_EQ(lev.distance("hellp"), 1);  // o -> p
}

TEST(LevenshteinTest, SingleInsertion) {
  Levenshtein lev("hello");
  EXPECT_EQ(lev.distance("hell"), 1);   // delete o
  EXPECT_EQ(lev.distance("ello"), 1);   // delete h
  EXPECT_EQ(lev.distance("helo"), 1);   // delete one l
}

TEST(LevenshteinTest, SingleDeletion) {
  Levenshtein lev("hello");
  EXPECT_EQ(lev.distance("helloo"), 1);  // extra o
  EXPECT_EQ(lev.distance("hhello"), 1);  // extra h
  EXPECT_EQ(lev.distance("heello"), 1);  // extra e
}

TEST(LevenshteinTest, MultipleEdits) {
  Levenshtein lev("kitten");
  EXPECT_EQ(lev.distance("sitting"), 3);  // k->s, e->i, +g
}

TEST(LevenshteinTest, CompletelyDifferent) {
  Levenshtein lev("abc");
  EXPECT_EQ(lev.distance("xyz"), 3);  // All substitutions
}

// -----------------------------------------------------------------------------
// Newline Handling (for multi-line buffers)
// -----------------------------------------------------------------------------

TEST(LevenshteinTest, NewlineAsCharacter) {
  Levenshtein lev("aaa\nbbb");
  EXPECT_EQ(lev.distance("aaa\nbbb"), 0);
  EXPECT_EQ(lev.distance("aaabbb"), 1);   // Delete newline
  EXPECT_EQ(lev.distance("aaa\nccc"), 3); // bbb -> ccc
}

TEST(LevenshteinTest, LineJoin) {
  // Simulates J command in Vim
  Levenshtein lev("aaabbb");
  EXPECT_EQ(lev.distance("aaa\nbbb"), 1);  // Insert newline
}

TEST(LevenshteinTest, LineDelete) {
  // Simulates dd on middle line
  Levenshtein lev("aaa\nccc");
  EXPECT_EQ(lev.distance("aaa\nbbb\nccc"), 4);  // Insert "bbb\n"
}

// -----------------------------------------------------------------------------
// flattenLines Helper
// -----------------------------------------------------------------------------

TEST(LevenshteinTest, FlattenLinesEmpty) {
  EXPECT_EQ(Lines({}).flatten(), "");
}

TEST(LevenshteinTest, FlattenLinesSingle) {
  EXPECT_EQ(Lines({"hello"}).flatten(), "hello");
}

TEST(LevenshteinTest, FlattenLinesMultiple) {
  EXPECT_EQ(Lines({"aaa", "bbb", "ccc"}).flatten(), "aaa\nbbb\nccc");
}

TEST(LevenshteinTest, FlattenLinesWithEmptyLines) {
  EXPECT_EQ(Lines({"aaa", "", "ccc"}).flatten(), "aaa\n\nccc");
}

// -----------------------------------------------------------------------------
// Prefix Caching
// -----------------------------------------------------------------------------

TEST(LevenshteinTest, CachingProducesSameResults) {
  Levenshtein lev("hello world");

  // First query - no cache
  int d1 = lev.distance("hello earth");

  // Second query with shared prefix - should use cache
  int d2 = lev.distance("hello venus");

  // Third query - same as first, should still be correct
  int d3 = lev.distance("hello earth");

  EXPECT_EQ(d1, d3);  // Same query should give same result
  EXPECT_EQ(d1, 4);   // "world" -> "earth" = 4 edits (w↔e, o↔a, l↔t, d↔h)
  EXPECT_EQ(d2, 5);   // "world" -> "venus" = 5 edits
}

TEST(LevenshteinTest, ClearCacheWorks) {
  Levenshtein lev("goal");

  lev.distance("test1");
  lev.distance("test2");

  // Should not crash or produce wrong results after clearing
  lev.clearCache();

  EXPECT_EQ(lev.distance("goal"), 0);
  EXPECT_EQ(lev.distance("goa"), 1);
}

TEST(LevenshteinTest, DifferentPrefixesDontCollide) {
  Levenshtein lev("abcdefgh");

  // These have different prefixes, shouldn't interfere
  int d1 = lev.distance("abcXefgh");  // 1 edit at position 3
  int d2 = lev.distance("abYdefgh");  // 1 edit at position 2
  int d3 = lev.distance("Zbcdefgh");  // 1 edit at position 0

  EXPECT_EQ(d1, 1);
  EXPECT_EQ(d2, 1);
  EXPECT_EQ(d3, 1);
}

// -----------------------------------------------------------------------------
// Edge Cases
// -----------------------------------------------------------------------------

TEST(LevenshteinTest, VeryLongStrings) {
  std::string goal(100, 'a');
  std::string source(100, 'a');
  source[50] = 'b';  // One difference in the middle

  Levenshtein lev(goal);
  EXPECT_EQ(lev.distance(source), 1);
}

TEST(LevenshteinTest, GoalAccessor) {
  Levenshtein lev("test goal");
  EXPECT_EQ(lev.goal(), "test goal");
}

// -----------------------------------------------------------------------------
// Symmetry Property (Levenshtein is symmetric)
// -----------------------------------------------------------------------------

TEST(LevenshteinTest, SymmetryProperty) {
  // d(a, b) == d(b, a)
  Levenshtein lev1("hello");
  Levenshtein lev2("world");

  EXPECT_EQ(lev1.distance("world"), lev2.distance("hello"));
}
