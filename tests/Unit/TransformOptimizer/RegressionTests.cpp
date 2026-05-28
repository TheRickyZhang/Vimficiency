// tests/Unit/TransformOptimizer/RegressionTests.cpp
//
// Regression tests for TransformOptimizer edge cases.
// Run: ./build/tests/vimfy_unit_tests --gtest_filter="TransformOptimizerRegression.*"

#include <cassert>

#include <gtest/gtest.h>

#include <vector>

#include "Boundary/TransformBoundary.h"
#include "Effort/EffortBank.h"
#include "Interpreter/EditInterpreter.h"
#include "Keyboard/Config.h"
#include "Optimizer/TransformOptimizer/TransformExplorer.h"
#include "Optimizer/TransformOptimizer/TransformOptimizer.h"
#include "Optimizer/TransformOptimizer/TransformOptimizerParams.h"
#include "Optimizer/TransformOptimizer/TransformPostExplorerEmissions.h"
#include "Types/Mode.h"
#include "Types/CursorPos.h"
#include "Utils/NeovimOracle.h"
#include "Utils/OracleReplay.h"
#include "Types/Lines.h"
#include "VimCore/VimEndpointUtils.h"

using namespace std;

namespace {

struct EmbeddedCase {
  Lines fullBuffer;
  Lines editRegion;
  Lines goalLines;
  TransformBoundary boundary;
  CursorPos firstPos;
  CursorPos endPos;  // exclusive
};

// Hardcoded snapshot of the original `RandomGen::seed(465950)` fixture so
// the test is deterministic across libstdc++ and libc++.
// `std::uniform_int_distribution` is not specified to produce identical
// output across implementations even with an identical Mersenne Twister
// state, so seed-driven fixtures diverge between Linux and macOS.
EmbeddedCase buildSmallEmbeddedCase() {
  Lines fullBuffer = {
      "ee,.  c.",
      "d.eeb,b.,ac., a",
      "c.debfefb bcc,.",
      "f aa,, ad.a.e",
  };

  CursorPos firstPos(0, 4);   // prefixLen = min(4, 8/2) = 4
  CursorPos endPos(3, 9);     // (lastLine, len - min(4, 13/2)) = (3, 13 - 4)

  Lines editRegion = fullBuffer.getSpan(firstPos, endPos);
  TransformBoundary boundary(fullBuffer, firstPos, endPos);
  Lines goalLines;  // unused by the only consumer; kept for struct shape

  return {fullBuffer, editRegion, goalLines, boundary, firstPos, endPos};
}

CursorPos toFullBufferPos(CursorPos localPos, CursorPos regionBegin) {
  localPos.line += regionBegin.line;
  if (localPos.line == regionBegin.line) {
    localPos.col += regionBegin.col;
  }
  return localPos;
}

void expectResolvedBackwardWordDeleteMatchesOracle(
    NeovimOracle& oracle, const Lines& source, CursorPos start,
    string_view command, bool bigWord) {
  VimCore::WordBoundaryContext boundary;
  CharRange rawRange = VimCore::wordOperatorRange(
      start, source, VimCore::WordOperatorTarget::DeleteBackToWordBegin,
      bigWord, boundary);
  ASSERT_NE(rawRange.begin, POSITION_OUTSIDE_BOUNDARY);
  ASSERT_NE(rawRange.end, POSITION_OUTSIDE_BOUNDARY);

  auto resolved = VimCore::resolveBackwardExclusiveWordDeleteRange(
      rawRange.begin, start, source, boundary.contentStartCol(start.line));
  TransformEditorState state(source, start);
  TransformEditorState after =
      TransformSimulator::afterResolvedDeletion(state, resolved);

  SimulationResult expected =
      oracle.simulate(source, start.line, start.col, string(command));
  EXPECT_EQ(after.getLines(), expected.lines) << "command=" << command;
  EXPECT_EQ(after.getPos(), CursorPos(expected.row, expected.col))
      << "command=" << command;
  EXPECT_EQ(after.getMode(), expected.mode) << "command=" << command;
}

}  // namespace

TEST(TransformOptimizerRegression, BoundaryAwareReplayPrefixKeepsXApplicable) {
  EmbeddedCase test = buildSmallEmbeddedCase();

  // For this benchmark-derived shape, effective lines equal fullBuffer.
  Lines replayLines = test.editRegion;
  replayLines[0] = test.boundary.prefix() + replayLines[0];
  replayLines[replayLines.lastLine()] += test.boundary.suffix();
  ASSERT_EQ(replayLines, test.fullBuffer);

  int leftOffset = test.boundary.leftColOffset();
  ASSERT_GT(static_cast<int>(replayLines[0].size()), leftOffset + 1);

  CursorPos pos(0, leftOffset + 1);  // startIndex=1 in effective coordinates
  Mode mode = Mode::Normal;
  DotRepeat lastEdit;

  // Use a sequence that exercises boundary-aware prefix replay and leaves
  // `X` applicable afterward.
  auto parsedPrefix = Edit::parseEdits("DdaW");
  assert(parsedPrefix);
  for (const ParsedEdit& op : *parsedPrefix) {
    Edit::applyEdit(replayLines, pos, mode, op, &lastEdit,
                    test.boundary.hasLinesBelow(),
                    test.boundary.leftColOffset(),
                    test.boundary.rightColOffset(),
                    test.boundary.hasLinesAbove());
  }

  EXPECT_EQ(mode, Mode::Normal);
  EXPECT_EQ(lastEdit.base, "daW");
  EXPECT_EQ(lastEdit.count, 0);
  EXPECT_GT(pos.col, 0) << "Prefix replay should leave room for following X";

  const ParsedEdit x("X");
  Edit::applyEdit(replayLines, pos, mode, x, &lastEdit,
                  test.boundary.hasLinesBelow(),
                  test.boundary.leftColOffset(),
                  test.boundary.rightColOffset(),
                  test.boundary.hasLinesAbove());
  EXPECT_EQ(mode, Mode::Normal);
}

TEST(TransformOptimizerRegression, EmptySourceChangeDoesNotEmitEmptySequence) {
  Config config = Config::uniform();
  TransformOptimizer optimizer(config);
  NeovimOracle oracle;

  const Lines source = {""};
  const Lines goal = {";"};
  TransformBoundary boundary(source, CursorPos(0, 0), source.endPos());

  TransformResult result = optimizer.optimizeTransform(
      source, goal, boundary,
      TransformOptimizerParams{}
          .withMaxResults(10)
          .withMaxResultsPerStartPos(3));

  auto bucket = result.resultsAt(0, 0);
  ASSERT_FALSE(bucket.empty());
  EXPECT_FALSE(bucket.front().getSequence().empty());
  for (const Result& candidate : bucket) {
    EXPECT_TRUE(OracleReplay::matches(
        oracle, source, CursorPos(0, 0),
        candidate.getSequence().str(), goal,
        nullopt, Mode::Normal, "empty source change"))
        << "sequence=" << candidate.getSequence().str();
  }
}

TEST(TransformOptimizerRegression, VisualDeleteFallbackRejectsExclusiveNextLineStart) {
  Config config = Config::uniform();
  Lines effective = {"~YYY===", " "};

  auto visual = TransformPostExplorer::tryVisualDelete(
      effective, 1, 1, TransformOptimizerParams{}, config);

  EXPECT_FALSE(visual.has_value());
}

TEST(TransformOptimizerRegression, BackwardWordChangeFromEmptyLineMatchesVim) {
  Lines lines = {"~~~~", ""};
  CursorPos pos(1, 0);
  Mode mode = Mode::Normal;
  DotRepeat lastEdit;
  ParsedEdit cb("cb");

  Edit::applyEdit(lines, pos, mode, cb, &lastEdit);

  NeovimOracle oracle;
  SimulationResult expected = oracle.simulate({"~~~~", ""}, 1, 0, "cb");
  EXPECT_EQ(lines, expected.lines);
  EXPECT_EQ(pos, CursorPos(expected.row, expected.col));
  EXPECT_EQ(mode, Mode::Insert);
}

TEST(TransformOptimizerRegression, BackwardWordChangeReplayPreservesTrailingEmptyLine) {
  Config config = Config::uniform();
  TransformOptimizer optimizer(config);
  NeovimOracle oracle;

  const Lines source = {"~~~~", ""};
  const Lines goal = {"&", "\\"};
  TransformBoundary boundary(source, CursorPos(0, 0), source.endPos());

  TransformResult result = optimizer.optimizeTransform(
      source, goal, boundary,
      TransformOptimizerParams{}
          .withMaxResults(40)
          .withMaxResultsPerStartPos(3));

  auto bucket = result.resultsAt(1, 0);
  ASSERT_FALSE(bucket.empty());
  for (const Result& candidate : bucket) {
    EXPECT_TRUE(OracleReplay::matches(
        oracle, source, CursorPos(1, 0),
        candidate.getSequence().str(), goal,
        nullopt, Mode::Normal, "cb empty-line change"))
        << "sequence=" << candidate.getSequence().str();
  }
}

TEST(TransformOptimizerRegression, ForwardSentenceDeleteStopsAtBlankBoundary) {
  Config config = Config::uniform();
  TransformOptimizer optimizer(config);
  NeovimOracle oracle;

  const Lines source = {"++", "", " !    "};
  TransformBoundary boundary(source, CursorPos(0, 0), source.endPos());

  TransformResult result = optimizer.optimizePureDeletion(
      source, boundary,
      TransformOptimizerParams{}
          .withMaxResults(40)
          .withMaxResultsPerStartPos(3));

  auto bucket = result.resultsAt(0, 0);
  ASSERT_FALSE(bucket.empty());
  for (const Result& candidate : bucket) {
    EXPECT_TRUE(OracleReplay::matches(
        oracle, source, CursorPos(0, 0),
        candidate.getSequence().str(), Lines{""},
        nullopt, Mode::Normal, "d) blank sentence boundary"))
        << "sequence=" << candidate.getSequence().str();
  }
}

TEST(TransformOptimizerRegression, EmbeddedSentenceDeleteAcrossSuffixLineIsPruned) {
  Config config = Config::uniform();
  EffortBank bank(config);

  const Lines fullBuffer = {"  yo", "   ", " 8 M~"};
  const Lines editRegion = {" yo", "   ", " 8 M"};
  TransformBoundary boundary(fullBuffer, CursorPos(0, 1), CursorPos(2, 4));
  Lines effective = boundary.withBoundary(editRegion);
  ASSERT_EQ(effective, fullBuffer);

  TransformOptimizerParams params;
  TransformExplorer explorer(
      boundary, params, config, bank,
      boundary.leftColOffset(), boundary.rightColOffset());

  bool sawDParen = false;
  explorer.exploreSentenceEdits<true>(
      Edit::FORWARD_SENTENCE_EDITS, CursorPos(0, 1), effective,
      [&](const ResolvedEditAction&, const SequenceBinding& cmd) {
        if (cmd.base.seq.view() != "d)") return;
        sawDParen = true;
      });

  EXPECT_FALSE(sawDParen);
}

TEST(TransformOptimizerRegression, EmbeddedDgeWithBlankPrefixIsInvalid) {
  Config config = Config::uniform();
  EffortBank bank(config);

  const Lines fullBuffer = {" Q", "\""};
  const Lines editRegion = {"Q", "\""};
  TransformBoundary boundary(fullBuffer, CursorPos(0, 1), CursorPos(1, 1));
  Lines effective = boundary.withBoundary(editRegion);
  ASSERT_EQ(effective, fullBuffer);

  TransformOptimizerParams params;
  TransformExplorer explorer(
      boundary, params, config, bank,
      boundary.leftColOffset(), boundary.rightColOffset());

  bool sawDge = false;
  explorer.exploreWordEdits(
      Edit::BACKWARD_WORD_EDITS, CursorPos(1, 0), effective,
      [&](const ResolvedEditAction& action, const SequenceBinding& cmd) {
        if (cmd.base.seq.view() != "dge") return;
        sawDge = true;
        EXPECT_FALSE(action.deleteEffectValid);
      });

  EXPECT_TRUE(sawDge);
}

TEST(TransformOptimizerRegression, OneLineClearedShellRequiresBoundaryCursor) {
  Config config = Config::uniform();
  TransformOptimizer optimizer(config);
  NeovimOracle oracle;

  const Lines fullBuffer = {"<~ ~", "~ w", " XH  ~~"};
  const Lines editRegion = {"~ ~", "~ w", " XH "};
  const Lines goalLines = {"", "     ", ""};
  const Lines expectedFull = {"<", "     ", " ~~"};
  TransformBoundary boundary(fullBuffer, CursorPos(0, 1), CursorPos(2, 4));

  TransformResult result = optimizer.optimizeTransform(
      editRegion, goalLines, boundary,
      TransformOptimizerParams{}
          .withMaxResults(80)
          .withMaxResultsPerStartPos(3));

  auto bucket = result.resultsAt(2, 2);
  ASSERT_FALSE(bucket.empty());
  size_t checked = min<size_t>(3, bucket.size());
  for (size_t i = 0; i < checked; i++) {
    EXPECT_TRUE(OracleReplay::matches(
        oracle, fullBuffer, CursorPos(2, 2),
        bucket[i].getSequence().str(), expectedFull,
        nullopt, Mode::Normal, "one-line cleared shell cursor"))
        << "sequence=" << bucket[i].getSequence().str();
  }
}

TEST(TransformOptimizerRegression, CountedDwWithLinesBelowBoundaryMatchesLocalSemantics) {
  const ParsedEdit fourDw("dw", 4);

  Lines withBoundary = {" ecbb.abec"};
  CursorPos posWithBoundary(0, 0);
  Mode modeWithBoundary = Mode::Normal;
  DotRepeat lastEditWithBoundary;
  Edit::applyEdit(withBoundary, posWithBoundary, modeWithBoundary, fourDw,
                  &lastEditWithBoundary,
                  /*hasLinesBelow=*/true,
                  /*leftColOffset=*/0,
                  /*rightColOffset=*/0,
                  /*hasLinesAbove=*/false);

  Lines localOnly = {" ecbb.abec"};
  CursorPos posLocal(0, 0);
  Mode modeLocal = Mode::Normal;
  DotRepeat lastEditLocal;
  Edit::applyEdit(localOnly, posLocal, modeLocal, fourDw,
                  &lastEditLocal,
                  /*hasLinesBelow=*/false,
                  /*leftColOffset=*/0,
                  /*rightColOffset=*/0,
                  /*hasLinesAbove=*/false);

  EXPECT_EQ(withBoundary, localOnly);
  EXPECT_EQ(posWithBoundary, posLocal);
  EXPECT_EQ(modeWithBoundary, modeLocal);
}

TEST(TransformOptimizerRegression, ForwardSentenceGapAtBottomBoundaryIsPruned) {
  Lines effectiveLines = {
      "Xdd,",
      ",e .edd",
      "cac.  ",
  };

  CursorPos endpoint = VimCore::sentenceOperatorEndpoint(
      CursorPos(2, 4), effectiveLines, true, 0, true);

  EXPECT_EQ(endpoint, POSITION_OUTSIDE_BOUNDARY);
}

TEST(TransformOptimizerRegression, BackwardParagraphExplorerEmitsDLeftBrace) {
  Config config = Config::uniform();
  Lines lines = {"abc", "", "def"};
  TransformBoundary boundary(lines, CursorPos(0, 0), lines.endPos());
  TransformOptimizerParams params;
  EffortBank bank(config);
  TransformExplorer explorer(
      boundary, params, config, bank,
      boundary.leftColOffset(), boundary.rightColOffset());

  bool sawLinewiseDLeftBrace = false;
  bool sawUnexpectedCharwiseDLeftBrace = false;

  explorer.exploreParagraphEdits<false>(
      Edit::BACKWARD_PARAGRAPH_EDITS, CursorPos(2, 0), lines,
      [&](const ResolvedEditAction& action, const SequenceBinding& cmd) {
        const VimCore::ResolvedDeleteRange& resolved = action.deleteEffect;
        if (cmd.base.seq.view() == "d{") {
          if (resolved.kind != VimCore::ResolvedDeleteRangeKind::Linewise) {
            sawUnexpectedCharwiseDLeftBrace = true;
          } else {
            LineRange range = resolved.lineRange;
            if (range.beginLine == 1 && range.endLine == 2) {
              sawLinewiseDLeftBrace = true;
            }
          }
        }
      });

  EXPECT_TRUE(sawLinewiseDLeftBrace);
  EXPECT_FALSE(sawUnexpectedCharwiseDLeftBrace);
}

TEST(TransformOptimizerRegression, SuffixCacheExpandsMismatchedDotRepeatContext) {
  Config config = Config::uniform();
  TransformOptimizer optimizer(config);
  NeovimOracle oracle;

  const Lines fullBuffer = {"ce.d.d ", "aa.eb", "f. e", ".e ae"};
  const Lines editRegion = {".d.d ", "aa.eb", "f. e", ".e a"};
  const Lines goalLines = {"fbbccbae", "c.dead", "aaadd   ", "e.b,"};
  const Lines expectedFull = {
      "cefbbccbae",
      "c.dead",
      "aaadd   ",
      "e.b,e",
  };
  TransformBoundary boundary(fullBuffer, CursorPos(0, 2), CursorPos(3, 4));

  TransformResult result = optimizer.optimizeTransform(
      editRegion, goalLines, boundary,
      TransformOptimizerParams{}
          .withMaxResults(200)
          .withMaxResultsPerStartPos(3));

  auto bucket = result.resultsAt(2, 2);
  ASSERT_FALSE(bucket.empty());

  size_t checked = min<size_t>(3, bucket.size());
  for (size_t i = 0; i < checked; i++) {
    EXPECT_TRUE(OracleReplay::matches(
        oracle, fullBuffer, CursorPos(2, 2),
        bucket[i].getSequence().str(), expectedFull,
        nullopt, Mode::Normal, "transform suffix cache dot context"))
        << "sequence=" << bucket[i].getSequence().str();
  }
}

TEST(TransformOptimizerRegression, ParagraphChangeAtEofCollapsesIntoPrefixLine) {
  Config config = Config::uniform();
  TransformOptimizer optimizer(config);
  NeovimOracle oracle;

  const Lines fullBuffer = {
      "fe,d ,",
      "eba,c",
      "b cb",
      "c,dbea.f ",
  };
  const Lines editRegion = {
      "",
      "eba,c",
      "b cb",
      "c,dbea.f ",
  };
  const Lines goalLines = {"b cb"};
  const Lines expectedFull = {"fe,d ,b cb"};
  TransformBoundary boundary(fullBuffer, CursorPos(0, 6), CursorPos(3, 9));

  TransformResult result = optimizer.optimizeTransform(
      editRegion, goalLines, boundary,
      TransformOptimizerParams{}
          .withMaxResults(200)
          .withMaxResultsPerStartPos(3),
      0, 6, CursorPos(0, 9));

  auto bucket = result.resultsAt(1, 0);
  ASSERT_FALSE(bucket.empty());
  EXPECT_NE(bucket.front().getSequence().str().find("<BS>"), string::npos)
      << "sequence=" << bucket.front().getSequence().str();

  size_t checked = min<size_t>(3, bucket.size());
  for (size_t i = 0; i < checked; i++) {
    EXPECT_TRUE(OracleReplay::matches(
        oracle, fullBuffer, CursorPos(1, 0),
        bucket[i].getSequence().str(), expectedFull,
        CursorPos(0, 9), Mode::Normal,
        "paragraph c} prefix collapse"))
        << "sequence=" << bucket[i].getSequence().str();
  }
}

TEST(TransformOptimizerRegression, EmbeddedChangePrunesBoundaryEscapingContinuation) {
  Config config = Config::uniform();
  TransformOptimizer optimizer(config);
  NeovimOracle oracle;

  const Lines fullBuffer = {
      "bfa   ",
      "cbde",
      " a.",
      ", fded",
  };
  const Lines editRegion = {
      "   ",
      "cbde",
      " a.",
      ", f",
  };
  const Lines goalLines = {
      ".da e d,",
      "d.b,",
      "ccbcc.,f",
      "b.,b",
  };
  const Lines expectedFull = {
      "bfa.da e d,",
      "d.b,",
      "ccbcc.,f",
      "b.,bded",
  };
  TransformBoundary boundary(fullBuffer, CursorPos(0, 3), CursorPos(3, 3));

  TransformResult result = optimizer.optimizeTransform(
      editRegion, goalLines, boundary,
      TransformOptimizerParams{}
          .withMaxResults(40)
          .withMaxResultsPerStartPos(3));

  auto bucket = result.resultsAt(1, 1);

  size_t checked = min<size_t>(3, bucket.size());
  for (size_t i = 0; i < checked; i++) {
    EXPECT_TRUE(OracleReplay::matches(
        oracle, fullBuffer, CursorPos(1, 1),
        bucket[i].getSequence().str(), expectedFull,
        nullopt, Mode::Normal, "embedded change boundary preservation"))
        << "sequence=" << bucket[i].getSequence().str();
  }
}

TEST(TransformOptimizerRegression, LinewiseChangeDoesNotTypeIntoHiddenContext) {
  Config config = Config::uniform();
  TransformOptimizer optimizer(config);
  NeovimOracle oracle;

  const Lines fullBuffer = {
      "afaddcd,cbfa",
      " d. c,b,",
      ".b ,",
      " ,bfb.cf",
  };
  const Lines editRegion = {
      " d. c,b,",
      ".b ,",
      " ,bfb.cf",
  };
  const Lines goalLines = {".b ,"};
  const Lines expectedFull = {
      "afaddcd,cbfa",
      ".b ,",
  };
  TransformBoundary boundary(fullBuffer, CursorPos(1, 0), CursorPos(3, 8));

  TransformResult result = optimizer.optimizeTransform(
      editRegion, goalLines, boundary,
      TransformOptimizerParams{}
          .withMaxResults(50)
          .withMaxResultsPerStartPos(3),
      1, 0, CursorPos(1, 3));

  auto bucket = result.resultsAt(1, 1);
  ASSERT_FALSE(bucket.empty());
  for (const Result& candidate : bucket) {
    EXPECT_TRUE(OracleReplay::matches(
        oracle, fullBuffer, CursorPos(1, 1),
        candidate.getSequence().str(), expectedFull,
        nullopt, Mode::Normal, "linewise change hidden context"))
        << "sequence=" << candidate.getSequence().str();
  }
}

TEST(TransformOptimizerRegression, TailParagraphDeletePreservesCursorColumn) {
  Config config = Config::uniform();
  TransformOptimizer optimizer(config);
  NeovimOracle oracle;

  const Lines source = {
      "dfdee",
      " ac,",
      "b  a.a",
  };
  TransformBoundary boundary(source, CursorPos(0, 0), source.endPos());

  TransformResult result = optimizer.optimizePureDeletion(
      source, boundary,
      TransformOptimizerParams{}
          .withMaxResults(80)
          .withMaxResultsPerStartPos(3));

  auto bucket = result.resultsAt(1, 1);
  ASSERT_FALSE(bucket.empty());
  for (const Result& candidate : bucket) {
    EXPECT_TRUE(OracleReplay::matches(
        oracle, source, CursorPos(1, 1),
        candidate.getSequence().str(), Lines{""},
        nullopt, Mode::Normal, "tail paragraph delete cursor column"))
        << "sequence=" << candidate.getSequence().str();
  }
}

TEST(TransformOptimizerRegression, BackwardWordResolvedDeleteMatchesVimCursorPolicy) {
  NeovimOracle oracle;

  expectResolvedBackwardWordDeleteMatchesOracle(
      oracle, Lines{"abc", "   "}, CursorPos(1, 0), "db", false);
  expectResolvedBackwardWordDeleteMatchesOracle(
      oracle, Lines{"x", "   "}, CursorPos(1, 0), "dB", true);
}

TEST(TransformOptimizerRegression, BlankBufferPureDeletionCandidatesReplayToGoal) {
  Config config = Config::uniform();
  TransformOptimizer optimizer(config);
  NeovimOracle oracle;

  const Lines source = {"   ", "", "  "};
  TransformBoundary boundary(source, CursorPos(0, 0), source.endPos());
  TransformResult result = optimizer.optimizePureDeletion(
      source, boundary,
      TransformOptimizerParams{}
          .withMaxResults(120)
          .withMaxResultsPerStartPos(5));

  ASSERT_GT(result.resultCount(), 0u);
  for (size_t i = 0; i < result.resultCount(); i++) {
    const auto& bucket = result.getResults()[i];
    if (bucket.empty()) continue;
    CursorPos start = source.cursorFromFlatIndexClamped(static_cast<int>(i));
    ASSERT_TRUE(start.isValid()) << "bucket=" << i;
    for (const Result& candidate : bucket) {
      EXPECT_TRUE(OracleReplay::matches(
          oracle, source, start, candidate.getSequence().str(), Lines{""},
          nullopt, Mode::Normal, "blank pure deletion"))
          << "bucket=" << i << " start=" << start
          << " sequence=" << candidate.getSequence().str();
    }
  }
}

TEST(TransformOptimizerRegression, EmbeddedPureDeletionDoesNotUseVisualShortcutAcrossBoundary) {
  Config config = Config::uniform();
  TransformOptimizer optimizer(config);
  NeovimOracle oracle;

  const Lines fullBuffer = {"cdf .e.,", "d.a ", ",abb"};
  const Lines editRegion = {" .e.,", "d.a ", ",ab"};
  const Lines expectedFull = {"cdfb"};
  TransformBoundary boundary(fullBuffer, CursorPos(0, 3), CursorPos(2, 3));

  TransformResult result = optimizer.optimizePureDeletion(
      editRegion, boundary,
      TransformOptimizerParams{}
          .withMaxResults(100)
          .withMaxResultsPerStartPos(5));

  auto bucket = result.resultsAt(0, 0);
  ASSERT_FALSE(bucket.empty());
  for (const Result& candidate : bucket) {
    EXPECT_TRUE(OracleReplay::matches(
        oracle, fullBuffer, CursorPos(0, 3),
        candidate.getSequence().str(), expectedFull,
        nullopt, Mode::Normal, "embedded visual-delete shortcut"))
        << "sequence=" << candidate.getSequence().str();
  }
}

TEST(TransformOptimizerRegression, ContentOnlyPureDeletionKeepsParentLine) {
  Config config = Config::uniform();
  TransformOptimizer optimizer(config);
  NeovimOracle oracle;

  const Lines fullBuffer = {"prefix", "delete me"};
  const Lines editRegion = {"delete me"};
  const Lines expectedFull = {"prefix", ""};
  TransformBoundary boundary(fullBuffer, CursorPos(1, 0), CursorPos(1, 9));

  TransformResult result = optimizer.optimizePureDeletion(
      editRegion, boundary,
      TransformOptimizerParams{}
          .withMaxResults(40)
          .withMaxResultsPerStartPos(5)
          .withLinewisePureDeletion(false),
      1, 0, CursorPos(1, 0));

  auto bucket = result.resultsAt(1, 0);
  ASSERT_FALSE(bucket.empty());
  for (const Result& candidate : bucket) {
    EXPECT_TRUE(OracleReplay::matches(
        oracle, fullBuffer, CursorPos(1, 0),
        candidate.getSequence().str(), expectedFull,
        nullopt, Mode::Normal, "content-only pure deletion"))
        << "sequence=" << candidate.getSequence().str();
  }
}

TEST(TransformOptimizerRegression, StraddledPureDeletionAllResultsReplayToGoal) {
  Config config = Config::uniform();
  TransformOptimizer optimizer(config);
  NeovimOracle oracle;

  struct Case {
    CursorPos begin;
    CursorPos end;
    Lines goal;
  };

  const Lines fullBuffer = {
    "arstn arstn",
    "arstn arstn",
  };
  const vector<Case> cases = {
    {CursorPos(0, 5), CursorPos(1, 11), Lines{"arstn"}},
    {CursorPos(0, 5), CursorPos(1, 6), Lines{"arstnarstn"}},
  };

  for (const Case& test : cases) {
    Lines editRegion = fullBuffer.getSpan(test.begin, test.end);
    TransformBoundary boundary(fullBuffer, test.begin, test.end);

    TransformResult result = optimizer.optimizePureDeletion(
        editRegion, boundary,
        TransformOptimizerParams{}
            .withMaxResults(200)
            .withMaxResultsPerStartPos(5));

    ASSERT_GT(result.resultCount(), 0u);
    int checked = 0;
    for (size_t i = 0; i < result.resultCount(); i++) {
      const auto& bucket = result.getResults()[i];
      if (bucket.empty()) continue;

      CursorPos localPos = editRegion.cursorFromFlatIndexClamped(static_cast<int>(i));
      ASSERT_TRUE(localPos.isValid()) << "Invalid local position at bucket " << i;
      CursorPos fullPos = toFullBufferPos(localPos, test.begin);
      for (const Result& resultForStart : bucket) {
        EXPECT_TRUE(OracleReplay::matches(
            oracle, fullBuffer, fullPos, resultForStart.getSequence().str(),
            test.goal, nullopt, Mode::Normal,
            "straddled pure deletion all results"))
            << "begin=" << test.begin << " end=" << test.end
            << " bucket=" << i << " localPos=" << localPos
            << " fullPos=" << fullPos
            << " seq='" << resultForStart.getSequence() << "'";
        checked++;
      }
    }
    EXPECT_GT(checked, 0);
  }
}
