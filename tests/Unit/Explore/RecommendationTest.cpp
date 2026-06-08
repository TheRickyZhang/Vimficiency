#include "Unit/Explore/TestHelpers.h"

using namespace std;

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
  const auto unCapped = *parseOptimizerParamOverrides(
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
  // For an EOL pure insertion the composition frontier surfaces the direct
  // Insert-entering action `A` when the cursor is inside the line-scope
  // activation range. Under the single-action invariant the typed payload
  // (`!`) and the `<Esc>` are owned by Explore's Insert phase, not encoded
  // in the frontier recommendation.
  Lines initial{Line("hello")};
  Lines goal{Line("hello!")};
  auto view = makeView(initial, {0, 0}, goal, {0, 5});

  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  auto recs = view.recommendations(20);
  ASSERT_FALSE(recs.empty());

  const bool hasA = any_of(recs.begin(), recs.end(),
      [](const Suggestion& s) { return string_view(s.token) == "A"; });
  EXPECT_TRUE(hasA)
      << "Navigate phase must surface `A` as a single-action composition recommendation";

  // Frontier emits single actions only — never a motion prefix, typed payload,
  // or trailing <Esc>.
  for (const auto& s : recs) {
    string_view tok(s.token);
    EXPECT_EQ(tok.find("<Esc>"), string_view::npos) << tok;
    EXPECT_EQ(tok.find('!'), string_view::npos) << tok;
  }
}

TEST_F(ExploreViewTest, CompositionFrontierEmitsNoMotionPrefix) {
  // Composition recommendations are valid only at their own activation
  // region; the navigation step to reach that region is NavFrontier's job.
  // From a position multiple lines away from a new-line insertion site, the
  // composition frontier must not synthesize a `Nj` prefix into its tokens.
  Lines initial;
  for (int i = 0; i < 20; i++) initial.push_back(Line("line " + to_string(i)));
  Lines goal = initial;
  goal.insert(goal.begin() + 5, Line("X"));
  auto view = makeView(initial, {0, 0}, goal, {5, 0});

  auto recs = view.recommendations(100);
  for (const auto& s : recs) {
    string_view tok(s.token);
    EXPECT_EQ(tok.find("oX"), string_view::npos)
        << "composition frontier must not synthesize motion+insert tokens: " << tok;
    EXPECT_EQ(tok.find("<Esc>"), string_view::npos)
        << "composition frontier must not bundle <Esc>: " << tok;
  }
}

TEST_F(ExploreViewTest, CompositionRecommendationCostMatchesSingleAction) {
  // Under the single-action invariant a composition recommendation's cost is
  // the cost of typing exactly that one Vim action — no bundled payload.
  Lines initial{Line("hello")};
  Lines goal{Line("hello!")};
  auto view = makeView(initial, {0, 0}, goal, {0, 5});

  auto recs = view.recommendations(20);
  auto rec = find_if(recs.begin(), recs.end(), [](const Suggestion& s) {
    return string_view(s.token) == "A";
  });
  ASSERT_NE(rec, recs.end());

  EXPECT_NEAR(rec->costDiff, getEffort("A", config), 1e-9);
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

  const auto disableTransformCounts = *parseOptimizerParamOverrides(
      "transform:maxPrefixCount=0");
  auto withoutCounts = view.recommendations(
      100, &disableTransformCounts, SuggestionSortMode::Effort);
  EXPECT_FALSE(hasCountedDelete(withoutCounts))
      << "transform frontier must pass transform count-prefix settings: "
      << tokens(withoutCounts);
}

TEST_F(ExploreViewTest, TransformPhaseSurfacesReplaceCharForSingleCharacterDiff) {
  // TransformPostExplorer owns the finalize-time replacement emission; the
  // depth-1 frontier must surface it too.
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  ASSERT_TRUE(view.applyMovement("l").has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));

  auto recs = view.recommendations(20);
  ASSERT_FALSE(recs.empty());
  auto tokens = [](const vector<Suggestion>& items) {
    string out;
    for (const auto& item : items) {
      if (!out.empty()) out += ", ";
      out += item.token;
    }
    return out;
  };
  const bool hasReplaceChar = any_of(recs.begin(), recs.end(),
      [](const Suggestion& s) { return string_view(s.token) == "rB"; });
  const bool hasDeleteFirst = any_of(recs.begin(), recs.end(),
      [](const Suggestion& s) {
        return string_view(s.token) == "x";
      });
  EXPECT_TRUE(hasReplaceChar)
      << "depth-1 transform must emit `rB` for single-char same-length replacement: "
      << tokens(recs);
  EXPECT_TRUE(hasDeleteFirst)
      << "replacement transform must expose deletion-first prefixes like `x`";
}

TEST_F(ExploreViewTest, TransformFrontierDoesNotBundleInsertPayload) {
  Lines initial{Line("foo bar baz")};
  Lines goal{Line("foo Qbaz")};
  auto view = makeView(initial, {0, 4}, goal, {0, 4});

  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  auto recs = view.recommendations(30);
  ASSERT_FALSE(recs.empty());

  bool hasDeleteFirst = false;
  for (const auto& rec : recs) {
    string_view token(rec.token);
    EXPECT_EQ(token.find("<Esc>"), string_view::npos) << token;
    EXPECT_EQ(token.find('Q'), string_view::npos) << token;
    EXPECT_NE(token, "dwi");
    if (token == "dw") hasDeleteFirst = true;
  }
  EXPECT_TRUE(hasDeleteFirst)
      << "dw replacement should continue through Transform -> Insert phases";
}

TEST_F(ExploreViewTest, TransformFrontierExcludesVisualDeleteMacros) {
  Lines lines{Line("alpha"), Line("beta"), Line("gamma")};
  DiffState diff(
      CursorPos(0, 0), CursorPos(2, 5), lines.flatten(), "",
      TransformBoundary{});
  auto recs = rankTransformFrontier(
      TransformFrontierQuery{
          FrontierQuery{
              .lines = lines,
              .cursor = {0, 0},
              .maxCount = 50,
          },
          diff,
      },
      Config::uniform());
  ASSERT_FALSE(recs.empty());
  for (const auto& rec : recs) {
    ASSERT_FALSE(string_view(rec.token).empty());
    EXPECT_NE(string_view(rec.token).front(), 'v') << rec.token;
  }
}

TEST_F(ExploreViewTest, FrontierTokensArePairwiseDistinct) {
  // CompositionFrontier's CHECK guards the invariant in release builds; this
  // test exercises representative inputs to ensure no current emission path
  // produces a duplicate. Builds an Explore::View whose recommendations
  // include both NavFrontier and CompositionFrontier output.
  Lines initial{Line("foo (hello) bar"), Line("baz")};
  Lines goal{Line("foo (goodbye) bar"), Line("baz")};
  auto view = makeView(initial, {0, 0}, goal, {0, 4});

  auto recs = view.recommendations(50);
  ASSERT_FALSE(recs.empty());

  unordered_set<string> seen;
  for (const auto& rec : recs) {
    string token(rec.token);
    EXPECT_TRUE(seen.insert(token).second)
        << "duplicate token across frontier output: " << token;
  }
}

TEST_F(ExploreViewTest, RecommendationSortModesUseDifferentMetrics) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 1}, goal, {0, 1});

  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  const auto overrides = *parseOptimizerParamOverrides(
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

// A replacement whose edit region is two empty lines ("\n"): a bare J joins
// them to the cleared shell ([prefix+suffix] = [""]) that the goal text is then
// typed into. deleteToChangeChar has no J case, so routing J through the
// char-change conversion asserts; the frontier must emit raw "J".
TEST(TransformFrontier, JoinReachingClearedShellInReplacementEmitsRawJoin) {
  DiffState diff(CursorPos(0, 0), CursorPos(1, 0), "\n", "x", TransformBoundary{});
  auto recs = rankTransformFrontier(
      TransformFrontierQuery{
          FrontierQuery{
              .lines = Lines{Line(""), Line("")},
              .cursor = {0, 0},
              .maxCount = 10,
          },
          diff,
      },
      Config::uniform());
  bool hasJoin = std::any_of(recs.begin(), recs.end(),
      [](const Suggestion& s) { return s.token == "J"; });
  EXPECT_TRUE(hasJoin);
}

TEST_F(ExploreViewTest, NavigateMotionRecommendationsLandOnEditStarts) {
  Lines initial{Line("one two three four five six seven")};
  Lines goal{Line("one two three four FIVE six seven")};
  auto view = makeView(initial, {0, 0}, goal, {0, 22});

  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  const auto overrides = *parseOptimizerParamOverrides(
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

// Deleting the last char of a line as the prefix of a replacement clamps the
// Normal-mode cursor back one column, so the post-deletion cursor is NOT the
// residual insertion start. acceptSnapshot must derive the phase (-> Navigate)
// rather than forcing Transform, which recommendTransform would assert against.
TEST_F(ExploreViewTest, DeletePrefixClampedCursorRederivesPhaseNotAssert) {
  Lines initial{Line("abc")};
  Lines goal{Line("abX")};
  auto view = makeView(initial, {0, 2}, goal, {0, 2});

  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));

  // User deletes 'c' with `x`; Vim clamps the cursor from col 2 to col 1.
  auto outcome =
      view.acceptSnapshot(Lines{Line("ab")}, {0, 1}, "x", /*insertMode=*/false);
  ASSERT_TRUE(outcome.has_value());

  // Cursor (0,1) is not the insertion start (0,2): phase must fall back to
  // Navigate, and recommendations() must not abort.
  EXPECT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  auto recs = view.recommendations(10);
  EXPECT_FALSE(recs.empty());
}

} // namespace
