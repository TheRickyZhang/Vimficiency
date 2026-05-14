#include "Explore/TestHelpers.h"

using namespace std;
using ExploreTestSupport::ExploreViewTest;

namespace {

TEST_F(ExploreViewTest, RecommendationsHonorPerCellDedupOverride) {
  // End-to-end check that overrides reach the frontier: with default
  // dedup (cap 1), each landing cell carries one motion. Setting
  // `nav:maxResultsPerEndPos` to a large value lifts the cap so
  // multiple distinct motions to the same cell can surface.
  Lines initial{Line("one two three four five")};
  Lines goal{Line("one two three four FIVE")};
  auto view = makeView(initial, {0, 0}, goal, {0, 19});

  // Default — one motion per landing cell.
  auto deduped = view.recommendations(20);

  // Override — disable the cap.
  const auto unCapped = OptimizerParamOverrides::parse(
      "nav:maxResultsPerEndPos=2147483647");
  auto wide = view.recommendations(20, &unCapped);

  // The override-broadened set should reach at least as many
  // recommendations as the default set (more landing-cell coverage,
  // and/or multiple tokens per cell).
  ASSERT_FALSE(deduped.empty()) << "default dedup produced no recs";
  EXPECT_GE(wide.size(), deduped.size())
      << "override should not shrink the result set";
}

TEST_F(ExploreViewTest, RecommendationsAreDiverse) {
  Lines initial{Line("foo bar baz qux zed")};
  Lines goal{Line("foo bar baz qux ZED")};
  auto view = makeView(initial, {0, 0}, goal, {0, 18});

  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  auto recs = view.recommendations(5);
  ASSERT_FALSE(recs.empty()) << "expected at least one motion recommendation";

  // Distinct recommendation texts — grouping/dedup works.
  set<string> texts;
  for (const auto& rec : recs)
    texts.insert(rec.token);
  EXPECT_EQ(texts.size(), recs.size());

  // Navigate phase, so all recs are motions; each must change the cursor.
  for (const auto& rec : recs) {
    const bool moved = rec.landingPos.line != 0 || rec.landingPos.col != 0;
    EXPECT_TRUE(moved) << "motion '" << rec.token << "' did not change cursor";
  }
}

TEST_F(ExploreViewTest, RecommendationCostDiffIncludesAcceptedSequence) {
  config = Config::qwerty();
  config.weights.same_key_weight = 1.0;
  Lines lines{Line("foo bar baz")};
  auto view = makeView(lines, {0, 0}, lines, {0, 8});

  ASSERT_TRUE(view.applyMovement("w").has_value());

  auto recs = view.recommendations(5);
  auto rec = find_if(recs.begin(), recs.end(), [](const Suggestion& candidate) {
    return string_view(candidate.token) == "w";
  });
  ASSERT_NE(rec, recs.end());

  string combined = view.state().seq + string(rec->token);
  const double expected = getEffort(combined, config) - view.state().cost;
  const double standalone = getEffort(string(rec->token), config);

  EXPECT_NEAR(rec->costDiff, expected, 1e-9);
  EXPECT_GT(abs(rec->costDiff - standalone), 1e-9);
}

TEST_F(ExploreViewTest, NavigatePhaseSurfacesACompositionForEolInsertion) {
  // For an EOL pure insertion the optimizer telescopes Navigate+Transform
  // by emitting `A<text>` from any column on the line. The composition
  // frontier should surface this during Navigate phase so the user sees
  // the shortcut alongside pure motions toward the insertion point.
  Lines initial{Line("hello")};
  Lines goal{Line("hello!")};
  auto view = makeView(initial, {0, 0}, goal, {0, 5});

  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  auto recs = view.recommendations(20);
  ASSERT_FALSE(recs.empty());

  // Composition motions include trailing <Esc> for the typed-payload exit
  // (mirrors what the optimizer emits in its flat sequences).
  const bool hasA = any_of(recs.begin(), recs.end(),
      [](const Suggestion& s) { return string_view(s.token) == "A!<Esc>"; });
  const bool hasMotionA = any_of(recs.begin(), recs.end(),
      [](const Suggestion& s) { return string_view(s.token) == "$a!<Esc>"; });
  EXPECT_TRUE(hasA)
      << "Navigate phase must surface `A!<Esc>` composition motion (zero-prefix)";
  EXPECT_TRUE(hasMotionA)
      << "Navigate phase must surface `$a!<Esc>` composition motion (motion-prefixed)";
}

TEST_F(ExploreViewTest, NavigateCompositionHonorsCompositionCountPrefixOverrides) {
  Lines initial;
  for (int i = 0; i < 20; i++) initial.push_back(Line("line " + to_string(i)));
  Lines goal = initial;
  goal.insert(goal.begin() + 5, Line("X"));
  auto view = makeView(initial, {0, 0}, goal, {5, 0});

  auto hasCountedLineInsert = [](const vector<Suggestion>& recs) {
    return any_of(recs.begin(), recs.end(), [](const Suggestion& s) {
      string_view token(s.token);
      return token.rfind("4j", 0) == 0 &&
             token.find("oX<Esc>") != string_view::npos;
    });
  };
  auto tokens = [](const vector<Suggestion>& recs) {
    string out;
    for (const auto& rec : recs) {
      if (!out.empty()) out += ", ";
      out += rec.token;
    }
    return out;
  };

  auto defaultRecs = view.recommendations(100);
  EXPECT_TRUE(hasCountedLineInsert(defaultRecs)) << tokens(defaultRecs);

  const auto disableCompositionCounts = OptimizerParamOverrides::parse(
      "composition:maxPrefixCount=0");
  auto withoutCounts = view.recommendations(
      100, &disableCompositionCounts, SuggestionSortMode::Effort);
  EXPECT_FALSE(hasCountedLineInsert(withoutCounts))
      << "composition frontier must pass composition count-prefix settings into nav: "
      << tokens(withoutCounts);
}

TEST_F(ExploreViewTest, CompositionRecommendationCostIncludesFullShortcut) {
  Lines initial{Line("hello")};
  Lines goal{Line("hello!")};
  auto view = makeView(initial, {0, 0}, goal, {0, 5});

  auto recs = view.recommendations(20);
  auto rec = find_if(recs.begin(), recs.end(), [](const Suggestion& s) {
    return string_view(s.token) == "$a!<Esc>";
  });
  ASSERT_NE(rec, recs.end());

  EXPECT_NEAR(rec->costDiff, getEffort("$a!<Esc>", config), 1e-9);
  EXPECT_GT(rec->costDiff, getEffort("$", config));
}

TEST_F(ExploreViewTest, TransformDeletionHonorsTransformCountPrefixOverrides) {
  Lines initial{Line("a"), Line("b"), Line("c"), Line("d"), Line("e")};
  Lines goal{Line("e")};
  auto view = makeView(initial, {0, 0}, goal, {0, 0});

  auto hasCountedDelete = [](const vector<Suggestion>& recs) {
    return any_of(recs.begin(), recs.end(), [](const Suggestion& s) {
      return string_view(s.token) == "4dd";
    });
  };
  auto tokens = [](const vector<Suggestion>& recs) {
    string out;
    for (const auto& rec : recs) {
      if (!out.empty()) out += ", ";
      out += rec.token;
    }
    return out;
  };

  auto defaultRecs = view.recommendations(100);
  EXPECT_TRUE(hasCountedDelete(defaultRecs)) << tokens(defaultRecs);

  const auto disableTransformCounts = OptimizerParamOverrides::parse(
      "transform:maxPrefixCount=0");
  auto withoutCounts = view.recommendations(
      100, &disableTransformCounts, SuggestionSortMode::Effort);
  EXPECT_FALSE(hasCountedDelete(withoutCounts))
      << "transform frontier must pass transform count-prefix settings: "
      << tokens(withoutCounts);
}

TEST_F(ExploreViewTest, TransformPhaseSurfacesReplaceCharForSingleCharDiff) {
  // TransformPostExplorer owns the finalize-time replacement emission; the
  // depth-1 frontier must surface it too.
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  ASSERT_TRUE(view.applyMovement("l").has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));

  auto recs = view.recommendations(20);
  ASSERT_FALSE(recs.empty());
  const bool hasReplaceChar = any_of(recs.begin(), recs.end(),
      [](const Suggestion& s) { return string_view(s.token) == "rB"; });
  const bool hasDeleteFirst = any_of(recs.begin(), recs.end(),
      [](const Suggestion& s) {
        return string_view(s.token) == "x";
      });
  EXPECT_TRUE(hasReplaceChar)
      << "depth-1 transform must emit `rB` for single-char same-length replacement";
  EXPECT_TRUE(hasDeleteFirst)
      << "replacement transform must expose deletion-first prefixes like `x`";
}

TEST_F(ExploreViewTest, RecommendationSortModesUseDifferentMetrics) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 1}, goal, {0, 1});

  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  const auto overrides = OptimizerParamOverrides::parse(
      "transform:distanceWeight=2.0");

  auto indexOf = [](const vector<Suggestion>& recs, string_view token) {
    for (size_t i = 0; i < recs.size(); ++i) {
      if (string_view(recs[i].token) == token) return static_cast<int>(i);
    }
    return -1;
  };

  auto effort = view.recommendations(20, &overrides, SuggestionSortMode::Effort);
  auto distance = view.recommendations(20, &overrides, SuggestionSortMode::Distance);
  auto score = view.recommendations(20, &overrides, SuggestionSortMode::Score);

  const int effortReplace = indexOf(effort, "rB");
  ASSERT_GE(effortReplace, 0);

  const auto incomplete = find_if(effort.begin(), effort.end(),
      [&](const Suggestion& s) {
        return s.distance > 0.0 && s.costDiff < effort[effortReplace].costDiff;
      });
  ASSERT_NE(incomplete, effort.end());
  const string incompleteToken = incomplete->token;

  const int effortIncomplete = indexOf(effort, incompleteToken);
  const int distanceIncomplete = indexOf(distance, incompleteToken);
  const int distanceReplace = indexOf(distance, "rB");
  const int scoreIncomplete = indexOf(score, incompleteToken);
  const int scoreReplace = indexOf(score, "rB");

  ASSERT_GE(effortIncomplete, 0);
  ASSERT_GE(distanceIncomplete, 0);
  ASSERT_GE(distanceReplace, 0);
  ASSERT_GE(scoreIncomplete, 0);
  ASSERT_GE(scoreReplace, 0);

  EXPECT_LT(effortIncomplete, effortReplace);
  EXPECT_LT(distanceReplace, distanceIncomplete);
  EXPECT_LT(scoreReplace, scoreIncomplete);
  EXPECT_GT(effort[effortIncomplete].distance, 0.0);
  EXPECT_EQ(effort[effortReplace].distance, 0.0);
}

TEST(ExtractStructuralToken, ReturnsFirstNonTypedTextToken) {
  EXPECT_EQ(extractStructuralToken("sm<Esc>"), "s");
  EXPECT_EQ(extractStructuralToken("clm<Esc>"), "cl");
  EXPECT_EQ(extractStructuralToken("clfoo<Esc>"), "cl");
  EXPECT_EQ(extractStructuralToken("Jj"), "J");
  EXPECT_EQ(extractStructuralToken("x"), "x");
  EXPECT_EQ(extractStructuralToken("rm"), "rm");
  EXPECT_EQ(extractStructuralToken(""), "");
}

TEST(TransformFrontier, PreservesDistinctResultsFromSameStart) {
  DiffState diff(CursorPos(0, 0), CursorPos(0, 1), "x", "foo", TransformBoundary{});
  // Test's intent: verify that multiple distinct command-shape tokens
  // reaching the same diff goal are preserved by the frontier.
  // TransformExplorer enumerates per-shape lanes inherently, so no
  // override is needed to surface multiple tokens.
  auto recs = rankTransformFrontier(
      TransformFrontierQuery{
          FrontierQuery{
              .lines = Lines{Line("x")},
              .cursor = {0, 0},
              .maxCount = 10,
          },
          diff,
      },
      Config::uniform());
  ASSERT_GE(recs.size(), 2u);
  // Distinct structural tokens — the test's intent: multiple strategies for
  // the same diff produce different command shapes, all preserved.
  EXPECT_NE(recs[0].token, recs[1].token);
}

TEST_F(ExploreViewTest, NavigateMotionRecommendationsLandOnEditStarts) {
  Lines initial{Line("one two three four five six seven")};
  Lines goal{Line("one two three four FIVE six seven")};
  auto view = makeView(initial, {0, 0}, goal, {0, 22});

  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  const auto overrides = OptimizerParamOverrides::parse(
      "nav:maxResultsPerEndPos=2147483647");
  auto recs = view.recommendations(10, &overrides, SuggestionSortMode::Score);
  ASSERT_FALSE(recs.empty());

  set<string> seen;
  int motionCount = 0;
  bool hasRecoveryMotion = false;
  for (const auto& rec : recs) {
    ASSERT_TRUE(seen.insert(string(rec.token)).second)
        << "recommendations should be distinct";

    auto parsed = parseSequence(rec.token);
    if (!parsed) continue;
    const bool isMotion = all_of(parsed->begin(), parsed->end(),
        [](const TaggedToken& token) {
          return token.kind == TokenKind::Movement;
        });
    if (!isMotion) continue;

    motionCount++;
    auto probe = makeView(initial, {0, 0}, goal, {0, 22});
    auto moved = probe.acceptCursorMove(rec.landingPos, rec.token);
    ASSERT_TRUE(moved.has_value()) << "motion rec rejected: " << rec.token;
    if (std::holds_alternative<Explore::Transform>(probe.phase())) {
      hasRecoveryMotion = true;
    }
    EXPECT_EQ(probe.state().cursor, rec.landingPos);
  }
  EXPECT_GT(motionCount, 0);
  EXPECT_TRUE(hasRecoveryMotion);
}

} // namespace
