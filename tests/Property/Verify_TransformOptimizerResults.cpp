// Property: TransformOptimizer top results for generated edit-region problems
// must replay in Neovim to the exact goal while preserving protected full-buffer
// prefix/suffix context around embedded regions.

#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Boundary/TransformBoundary.h"
#include "Keyboard/Config.h"
#include "Optimizer/TransformOptimizer/TransformOptimizer.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Utils/NeovimOracle.h"
#include "Property/OptimizerResultChecks.h"
#include "Property/PropertyDomains.h"
#include "TransformOptimizer/EmbeddedRegionTestUtils.h"

using namespace std;

namespace {

struct FullBufferSpec {
  vector<string> source;
};

struct SameLengthReplacementSpec {
  string original;
  string replacement;
};

struct SingleLineChangeSpec {
  string source;
  string goal;
};

struct SameLineCountChangeSpec {
  vector<string> source;
  vector<string> goal;
};

struct EmbeddedRegionSpec {
  vector<string> editRegion;
  string prefix;
  string suffix;
};

struct EmbeddedChangeSpec {
  vector<string> editRegion;
  vector<string> goal;
  string prefix;
  string suffix;
};

Lines resizeLines(vector<string> rawLines, int lineCount) {
  Lines lines(std::move(rawLines));
  while (static_cast<int>(lines.size()) < lineCount) {
    lines.push_back("");
  }
  while (static_cast<int>(lines.size()) > lineCount) {
    lines.pop_back();
  }
  return lines;
}

string normalizedNonEmpty(string text, char fallback) {
  if (text.empty()) {
    text.push_back(fallback);
  }
  return text;
}

SameLengthReplacementSpec normalizeSameLength(
    SameLengthReplacementSpec spec) {
  spec.original = normalizedNonEmpty(std::move(spec.original), 'a');
  spec.replacement = normalizedNonEmpty(std::move(spec.replacement), 'b');
  if (spec.replacement.size() < spec.original.size()) {
    spec.replacement.append(
        spec.original.size() - spec.replacement.size(), spec.replacement.back());
  }
  if (spec.replacement.size() > spec.original.size()) {
    spec.replacement.resize(spec.original.size());
  }
  if (spec.replacement == spec.original) {
    spec.replacement[0] = spec.replacement[0] == 'a' ? 'b' : 'a';
  }
  return spec;
}

EmbeddedEditRegion toEmbeddedRegion(EmbeddedRegionSpec spec) {
  EmbeddedEditRegion result;
  result.editRegion = Lines(std::move(spec.editRegion));
  result.prefix = std::move(spec.prefix);
  result.suffix = std::move(spec.suffix);

  result.fullBuffer.reserve(result.editRegion.size());
  for (int i = 0; i <= result.editRegion.lastLine(); i++) {
    string line = result.editRegion[i];
    if (i == 0) line.insert(0, result.prefix);
    if (i == result.editRegion.lastLine()) line += result.suffix;
    result.fullBuffer.push_back(line);
  }

  result.startLine = 0;
  result.startCol = static_cast<int>(result.prefix.size());
  result.endLine = result.editRegion.lastLine();
  result.endCol = static_cast<int>(result.fullBuffer[result.endLine].size()) -
                  static_cast<int>(result.suffix.size());

  return result;
}

EmbeddedEditRegion toEmbeddedRegion(const EmbeddedChangeSpec& spec) {
  return toEmbeddedRegion(
      EmbeddedRegionSpec{spec.editRegion, spec.prefix, spec.suffix});
}

Lines goalForEmbeddedChange(const EmbeddedChangeSpec& spec, int lineCount) {
  return resizeLines(spec.goal, lineCount);
}

string contextForLines(const string& label, const Lines& source, const Lines& goal) {
  ostringstream out;
  out << label << " source=" << source << " goal=" << goal;
  return out.str();
}

string contextForEmbedded(
    const string& label,
    const EmbeddedEditRegion& region,
    const Lines& goal) {
  ostringstream out;
  out << label
      << " prefix='" << region.prefix << "'"
      << " suffix='" << region.suffix << "'"
      << " editRegion=" << region.editRegion
      << " fullBuffer=" << region.fullBuffer
      << " goal=" << goal;
  return out.str();
}

auto FullBufferSpecDomain() {
  return fuzztest::StructOf<FullBufferSpec>(
      PropertyDomains::LineVecDomain(2, 3, 0, 8));
}

auto SameLengthReplacementSpecDomain() {
  return fuzztest::StructOf<SameLengthReplacementSpec>(
      PropertyDomains::LineTextDomain(0, 9),
      PropertyDomains::LineTextDomain(0, 9));
}

auto SingleLineChangeSpecDomain() {
  return fuzztest::StructOf<SingleLineChangeSpec>(
      PropertyDomains::LineTextDomain(0, 8),
      PropertyDomains::LineTextDomain(1, 7));
}

auto SameLineCountChangeSpecDomain() {
  return fuzztest::StructOf<SameLineCountChangeSpec>(
      PropertyDomains::LineVecDomain(2, 3, 0, 8),
      PropertyDomains::LineVecDomain(1, 3, 0, 8));
}

auto EmbeddedRegionSpecDomain(
    int minLines, int maxLines, int minLineLen, int maxLineLen) {
  return fuzztest::StructOf<EmbeddedRegionSpec>(
      PropertyDomains::LineVecDomain(
          minLines, maxLines, minLineLen, maxLineLen),
      PropertyDomains::LineTextDomain(0, 3),
      PropertyDomains::LineTextDomain(0, 3));
}

auto EmbeddedChangeSpecDomain() {
  return fuzztest::StructOf<EmbeddedChangeSpec>(
      PropertyDomains::LineVecDomain(2, 4, 3, 5),
      PropertyDomains::LineVecDomain(1, 4, 0, 8),
      PropertyDomains::LineTextDomain(0, 3),
      PropertyDomains::LineTextDomain(0, 3));
}

class TransformOptimizerGeneratedPropertyTest {
 public:
  void SingleLineEmbeddedTopResultsReplay(const EmbeddedRegionSpec& spec) {
    auto test = toEmbeddedRegion(spec);
    TransformResult res = pureDeletionResult(test.editRegion, test.makeBoundary());
    Lines expected{test.expectedAfterDeletion()};
    int checkedStarts = 0;
    string context = contextForEmbedded("single-line embedded", test, expected);

    for (size_t i = 0; i < res.resultCount(); i++) {
      const auto& bucket = res.getResults()[i];
      if (bucket.empty()) continue;

      CursorPos editPos = fromFlatIndex(static_cast<int>(i), test.editRegion);
      CursorPos bufferPos = test.toFullBufferPos(editPos);
      expectEmittedTopResultsReachGoal(
          bucket, test.fullBuffer, bufferPos, expected, context, checkedStarts);
    }
    EXPECT_GT(checkedStarts, 0);
  }

  void MultiLineFullBufferTopResultsReplay(const FullBufferSpec& spec) {
    Lines source(spec.source);
    TransformBoundary boundary(source, {0, 0}, source.endPos());
    TransformResult res = pureDeletionResult(source, boundary);
    Lines goal{""};
    int checkedStarts = 0;
    string context = contextForLines("multi-line full buffer", source, goal);

    for (size_t i = 0; i < res.resultCount(); i += 2) {
      const auto& bucket = res.getResults()[i];
      if (bucket.empty()) continue;

      CursorPos pos = fromFlatIndex(static_cast<int>(i), source);
      expectEmittedTopResultsReachGoal(
          bucket, source, pos, goal, context, checkedStarts);
    }
    EXPECT_GT(checkedStarts, 0);
  }

  void SameLengthReplacementTopResultsReplay(
      SameLengthReplacementSpec rawSpec) {
    SameLengthReplacementSpec spec = normalizeSameLength(std::move(rawSpec));
    Lines source = {spec.original};
    Lines goal = {spec.replacement};
    TransformBoundary boundary(source, {0, 0}, source.endPos());
    TransformResult res = opt_.optimizeTransform(source, goal, boundary, params_);

    int checkedStarts = 0;
    expectEmittedTopResultsReachGoal(
        res.getResults()[0], source, CursorPos(0, 0), goal,
        contextForLines("same-length replacement", source, goal), checkedStarts);
    EXPECT_GT(checkedStarts, 0);
  }

  void MultiLineEmbeddedTopResultsReplay(const EmbeddedRegionSpec& spec) {
    auto test = toEmbeddedRegion(spec);
    TransformResult res = pureDeletionResult(test.editRegion, test.makeBoundary());
    Lines expected{test.expectedAfterDeletion()};
    int checkedStarts = 0;
    string context = contextForEmbedded("multi-line embedded", test, expected);

    for (size_t i = 0; i < res.resultCount(); i += 4) {
      const auto& bucket = res.getResults()[i];
      if (bucket.empty()) continue;

      CursorPos editPos = fromFlatIndex(static_cast<int>(i), test.editRegion);
      CursorPos bufferPos = test.toFullBufferPos(editPos);
      expectEmittedTopResultsReachGoal(
          bucket, test.fullBuffer, bufferPos, expected, context, checkedStarts);
    }
    EXPECT_GT(checkedStarts, 0);
  }

  void SingleLineChangeTopResultsReplay(const SingleLineChangeSpec& spec) {
    Lines source = {spec.source};
    Lines goal = {spec.goal};
    if (source == goal) return;

    TransformBoundary boundary(source, {0, 0}, source.endPos());
    TransformResult res = opt_.optimizeTransform(source, goal, boundary, params_);
    int checkedStarts = 0;
    string context = contextForLines("single-line change", source, goal);

    for (size_t i = 0; i < res.resultCount(); i++) {
      const auto& bucket = res.getResults()[i];
      if (bucket.empty()) continue;

      CursorPos pos = fromFlatIndex(static_cast<int>(i), source);
      expectEmittedTopResultsReachGoal(
          bucket, source, pos, goal, context, checkedStarts);
    }
    EXPECT_GT(checkedStarts, 0);
  }

  void MultiLineFullBufferChangeTopResultsReplay(
      const SameLineCountChangeSpec& spec) {
    Lines source(spec.source);
    Lines goal = resizeLines(spec.goal, static_cast<int>(source.size()));
    if (source == goal) return;

    TransformBoundary boundary(source, {0, 0}, source.endPos());
    TransformResult res = opt_.optimizeTransform(source, goal, boundary, params_);
    int checkedStarts = 0;
    string context = contextForLines("multi-line full-buffer change", source, goal);

    for (size_t i = 0; i < res.resultCount(); i += 4) {
      const auto& bucket = res.getResults()[i];
      if (bucket.empty()) continue;

      CursorPos pos = fromFlatIndex(static_cast<int>(i), source);
      expectEmittedTopResultsReachGoal(
          bucket, source, pos, goal, context, checkedStarts);
    }
    EXPECT_GT(checkedStarts, 0);
  }

  void MultiLineEmbeddedChangeTopResultsReplay(const EmbeddedChangeSpec& spec) {
    auto test = toEmbeddedRegion(spec);
    int numGoalLines = static_cast<int>(test.editRegion.size());
    Lines goal = goalForEmbeddedChange(spec, numGoalLines);
    if (goal == test.editRegion) return;

    TransformResult res =
        opt_.optimizeTransform(test.editRegion, goal, test.makeBoundary(), params_);

    Lines expectedFull;
    for (int i = 0; i < numGoalLines; i++) {
      string line;
      if (i == 0) line += test.prefix;
      line += goal[i];
      if (i == numGoalLines - 1) line += test.suffix;
      expectedFull.push_back(line);
    }

    int checkedStarts = 0;
    string context =
        contextForEmbedded("multi-line embedded change", test, expectedFull);
    for (size_t i = 0; i < res.resultCount(); i += 4) {
      const auto& bucket = res.getResults()[i];
      if (bucket.empty()) continue;

      CursorPos editPos = fromFlatIndex(static_cast<int>(i), test.editRegion);
      CursorPos bufferPos = test.toFullBufferPos(editPos);
      expectEmittedTopResultsReachGoal(
          bucket, test.fullBuffer, bufferPos, expectedFull, context, checkedStarts);
    }
    EXPECT_GT(checkedStarts, 0);
  }

 private:
  static constexpr size_t MAX_RESULTS_PER_START_TO_REPLAY = 3;

  Config config_ = Config::uniform();
  TransformOptimizerParams params_{};
  TransformOptimizer opt_{config_};
  NeovimOracle oracle_{};

  TransformResult pureDeletionResult(
      const Lines& initialLines, TransformBoundary boundary) {
    return opt_.optimizePureDeletion(initialLines, boundary, params_);
  }

  void expectEmittedTopResultsReachGoal(
      const vector<Result>& bucket,
      const Lines& initial,
      CursorPos initialPos,
      const Lines& goal,
      const string& context,
      int& checkedStarts) {
    if (bucket.empty()) return;
    checkedStarts++;
    expectTopResultsReplay(
        oracle_, bucket, initial, initialPos, goal,
        MAX_RESULTS_PER_START_TO_REPLAY, context);
  }
};

}  // namespace

FUZZ_TEST_F(TransformOptimizerGeneratedPropertyTest, SingleLineEmbeddedTopResultsReplay)
    .WithDomains(EmbeddedRegionSpecDomain(1, 1, 2, 4));

FUZZ_TEST_F(TransformOptimizerGeneratedPropertyTest, MultiLineFullBufferTopResultsReplay)
    .WithDomains(FullBufferSpecDomain());

FUZZ_TEST_F(TransformOptimizerGeneratedPropertyTest, SameLengthReplacementTopResultsReplay)
    .WithDomains(SameLengthReplacementSpecDomain());

FUZZ_TEST_F(TransformOptimizerGeneratedPropertyTest, MultiLineEmbeddedTopResultsReplay)
    .WithDomains(EmbeddedRegionSpecDomain(2, 4, 3, 5));

FUZZ_TEST_F(TransformOptimizerGeneratedPropertyTest, SingleLineChangeTopResultsReplay)
    .WithDomains(SingleLineChangeSpecDomain());

FUZZ_TEST_F(
    TransformOptimizerGeneratedPropertyTest,
    MultiLineFullBufferChangeTopResultsReplay)
    .WithDomains(SameLineCountChangeSpecDomain());

FUZZ_TEST_F(TransformOptimizerGeneratedPropertyTest, MultiLineEmbeddedChangeTopResultsReplay)
    .WithDomains(EmbeddedChangeSpecDomain());
