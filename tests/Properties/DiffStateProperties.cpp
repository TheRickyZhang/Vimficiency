#include <string_view>
#include <vector>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Optimizer/CompositionOptimizer/DiffState.h"
#include "Types/Lines.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"

using namespace std;

namespace {

Lines randomlyEdit(const Lines& initial) {
  Lines result = initial;

  int numEdits = RandomGen::range(1, 3);
  for (int e = 0; e < numEdits; e++) {
    int line = RandomGen::range(0, static_cast<int>(result.size()) - 1);
    string& s = result[line];
    int len = static_cast<int>(s.size());

    int editType = RandomGen::range(0, 2);
    if (editType == 0) {
      int pos = RandomGen::range(0, len);
      int insertLen = RandomGen::range(1, 5);
      string ins;
      for (int i = 0; i < insertLen; i++) {
        ins += RandomGen::pick<string_view>(
            {{80, CharPools::LETTERS}, {20, CharPools::SPACE}});
      }
      s.insert(pos, ins);
    } else if (editType == 1 && len > 0) {
      int a = RandomGen::range(0, len - 1);
      int delLen = min(RandomGen::range(1, 5), len - a);
      s.erase(a, delLen);
    } else if (len > 0) {
      int a = RandomGen::range(0, len - 1);
      int repLen = min(RandomGen::range(1, 5), len - a);
      string rep;
      for (int i = 0; i < repLen; i++) {
        rep += RandomGen::pick<string_view>(
            {{80, CharPools::LETTERS}, {20, CharPools::SPACE}});
      }
      s.replace(a, repLen, rep);
    }
  }

  return result;
}

void validateInvariants(
    const vector<DiffState>& diffs, const Lines& initial, const Lines& goal) {
  string startFlat = initial.flatten();

  for (size_t i = 0; i + 1 < diffs.size(); i++) {
    const auto& a = diffs[i];
    const auto& b = diffs[i + 1];
    EXPECT_TRUE(a.endPos.line < b.beginPos.line ||
                (a.endPos.line == b.beginPos.line &&
                 a.endPos.col <= b.beginPos.col))
        << "diff[" << i << "] overlaps diff[" << (i + 1) << "]";
  }

  for (size_t i = 0; i < diffs.size(); i++) {
    const auto& d = diffs[i];

    int typeCount = d.isPureInsertion() + d.isPureDeletion() + d.isReplacement();
    EXPECT_LE(typeCount, 1) << "diff[" << i << "] has multiple type flags";

    if (!d.deletedText.empty() || !d.insertedText.empty()) {
      EXPECT_EQ(typeCount, 1) << "diff[" << i << "] has no type flag";
    }

    if (d.isPureInsertion()) {
      EXPECT_EQ(d.beginPos, d.endPos)
          << "pure insertion diff[" << i << "] has non-empty range";
    }

    if (d.isPureDeletion() || d.isReplacement()) {
      EXPECT_NE(d.beginPos, d.endPos)
          << "deletion/replacement diff[" << i << "] has empty range";
    }

    if (d.hasDeletedContent()) {
      int flatBegin = DiffText::positionToFlatIndex(d.beginPos, initial);
      string actual = startFlat.substr(flatBegin, d.deletedText.size());
      EXPECT_EQ(actual, d.deletedText)
          << "diff[" << i << "] deletedText mismatch";
    }
  }
}

void SingleLineRoundTripAndStructure(uint32_t seed) {
  RandomGen::seed(seed);
  for (int caseIndex = 0; caseIndex < 100; caseIndex++) {
    SCOPED_TRACE(::testing::Message() << "seed=" << seed << " case=" << caseIndex);
    Lines initial = {randomLine(RandomGen::range(5, 30))};
    Lines goal = randomlyEdit(initial);
    if (initial == goal) continue;

    auto diffs = Myers::calculate(initial, goal);
    EXPECT_EQ(Myers::applyAllDiffState(diffs, initial), goal)
        << "Round-trip failed: '" << initial.flatten()
        << "' -> '" << goal.flatten() << "'";
    validateInvariants(diffs, initial, goal);
  }
}

void MultiLineRoundTripAndStructure(uint32_t seed) {
  RandomGen::seed(seed);
  for (int caseIndex = 0; caseIndex < 100; caseIndex++) {
    SCOPED_TRACE(::testing::Message() << "seed=" << seed << " case=" << caseIndex);
    int numLines = RandomGen::range(2, 6);
    Lines initial = randomLines(numLines, 3, 15);
    Lines goal = randomlyEdit(initial);
    if (initial == goal) continue;

    auto diffs = Myers::calculate(initial, goal);
    EXPECT_EQ(Myers::applyAllDiffState(diffs, initial), goal)
        << "Round-trip failed on multi-line input";
    validateInvariants(diffs, initial, goal);
  }
}

void CodeLikeRoundTripAndStructure(uint32_t seed) {
  RandomGen::seed(seed);
  for (int caseIndex = 0; caseIndex < 50; caseIndex++) {
    SCOPED_TRACE(::testing::Message() << "seed=" << seed << " case=" << caseIndex);
    Lines initial = randomCodeBuffer(RandomGen::range(3, 8), 15);
    Lines goal = randomlyEdit(initial);
    if (initial == goal) continue;

    auto diffs = Myers::calculate(initial, goal);
    EXPECT_EQ(Myers::applyAllDiffState(diffs, initial), goal)
        << "Round-trip failed on code-like input";
    validateInvariants(diffs, initial, goal);
  }
}

void IdenticalBuffersProduceNoDiffs(uint32_t seed) {
  RandomGen::seed(seed);
  for (int caseIndex = 0; caseIndex < 50; caseIndex++) {
    SCOPED_TRACE(::testing::Message() << "seed=" << seed << " case=" << caseIndex);
    Lines lines = randomLines(RandomGen::range(1, 4), 3, 15);
    auto diffs = Myers::calculate(lines, lines);
    EXPECT_EQ(diffs.size(), 0) << "Identical buffers should produce no diffs";
  }
}

Lines applySequentially(vector<DiffState> diffs, const Lines& initialLines) {
  Lines current = initialLines;
  OriginalDiffMapper mapper;

  for (const auto& originalDiff : diffs) {
    DiffState currentDiff = mapper.mapDiffToCurrent(
        originalDiff, initialLines, current);
    current = Myers::applyDiffState(currentDiff, current);
    mapper.recordApplied(originalDiff, initialLines);
  }

  return current;
}

void SequentialApplicationMatchesBatchApplication(uint32_t seed) {
  RandomGen::seed(seed);
  for (int caseIndex = 0; caseIndex < 200; caseIndex++) {
    SCOPED_TRACE(::testing::Message() << "seed=" << seed << " case=" << caseIndex);
    Lines initial = randomLines(RandomGen::range(1, 6), 3, 15);
    Lines goal = randomlyEdit(initial);
    if (initial == goal) continue;

    auto diffs = Myers::calculate(initial, goal);
    Lines expected = Myers::applyAllDiffState(diffs, initial);
    ASSERT_EQ(expected, goal) << "applyAllDiffState sanity check failed";

    Lines sequential = applySequentially(diffs, initial);
    EXPECT_EQ(sequential, expected)
        << "Sequential application failed"
        << "\ninitial: " << initial.flatten()
        << "\ngoal: " << goal.flatten()
        << "\nsequential: " << sequential.flatten()
        << "\ndiffs: " << diffs.size();
  }
}

}  // namespace

FUZZ_TEST(DiffStateGeneratedPropertyTest, SingleLineRoundTripAndStructure)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({42});

FUZZ_TEST(DiffStateGeneratedPropertyTest, MultiLineRoundTripAndStructure)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({43});

FUZZ_TEST(DiffStateGeneratedPropertyTest, CodeLikeRoundTripAndStructure)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({44});

FUZZ_TEST(DiffStateGeneratedPropertyTest, IdenticalBuffersProduceNoDiffs)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({45});

FUZZ_TEST(DiffStateGeneratedPropertyTest, SequentialApplicationMatchesBatchApplication)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({46});
