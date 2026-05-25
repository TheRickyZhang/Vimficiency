// Property: generated buffer pairs must diff/apply back to the exact goal, and
// each emitted DiffState must have coherent range/type/deleted-text metadata.
// Sequential application also has to match batch application because
// CompositionOptimizer applies planned edits against changing buffers.

#include <string>
#include <utility>
#include <vector>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Optimizer/CompositionOptimizer/DiffState.h"
#include "Property/PropertyDomains.h"
#include "Types/Lines.h"

using namespace std;

namespace {

struct DiffCase {
  Lines initial;
  Lines goal;
};

DiffCase toDiffCase(const PropertyDomains::DiffCaseSpec& spec) {
  Lines initial = PropertyDomains::toLines(spec.initial);
  Lines goal = spec.identity ? initial : PropertyDomains::toLines(spec.goal);
  return DiffCase{.initial = std::move(initial), .goal = std::move(goal)};
}

void validateInvariants(
    const vector<DiffState>& diffs, const Lines& initial, const Lines&) {
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
      EXPECT_NE(d.beginPos, d.endPos) << "deletion/replacement diff[" << i << "] has empty range";
    }

    if (d.hasDeletedContent()) {
      int flatBegin = DiffText::positionToFlatIndex(d.beginPos, initial);
      string actual = startFlat.substr(flatBegin, d.deletedText.size());
      EXPECT_EQ(actual, d.deletedText)
          << "diff[" << i << "] deletedText mismatch";
    }
  }
}

void RoundTripAndStructureAcrossBufferStyles(
    const PropertyDomains::DiffCaseSpec& spec) {
  DiffCase test = toDiffCase(spec);

  auto diffs = Myers::calculate(test.initial, test.goal);
  EXPECT_EQ(Myers::applyAllDiffState(diffs, test.initial), test.goal)
      << "Round-trip failed: '" << test.initial.flatten()
      << "' -> '" << test.goal.flatten() << "'";
  validateInvariants(diffs, test.initial, test.goal);

  // Identity is a semantic contract for the diff planner, not just an edge
  // case: consumers use empty diffs to mean "no edit phase required."
  if (test.initial == test.goal) {
    EXPECT_TRUE(diffs.empty());
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

void SequentialApplicationMatchesBatchApplication(
    const PropertyDomains::DiffCaseSpec& spec) {
  DiffCase test = toDiffCase(spec);
  if (test.initial == test.goal) return;

  auto diffs = Myers::calculate(test.initial, test.goal);
  Lines expected = Myers::applyAllDiffState(diffs, test.initial);
  ASSERT_EQ(expected, test.goal) << "applyAllDiffState sanity check failed";

  // Composition applies planned diffs one at a time against changing
  // fencepost buffers; this checks original-coordinate remapping.
  Lines sequential = applySequentially(diffs, test.initial);
  EXPECT_EQ(sequential, expected)
      << "Sequential application failed"
      << "\ninitial: " << test.initial.flatten()
      << "\ngoal: " << test.goal.flatten()
      << "\nsequential: " << sequential.flatten()
      << "\ndiffs: " << diffs.size();
}

}  // namespace

FUZZ_TEST(DiffStateGeneratedPropertyTest, RoundTripAndStructureAcrossBufferStyles)
    .WithDomains(PropertyDomains::DiffCaseSpecDomain());

FUZZ_TEST(DiffStateGeneratedPropertyTest, SequentialApplicationMatchesBatchApplication)
    .WithDomains(PropertyDomains::DiffCaseSpecDomain());
