// Property: RunningEffort composition must be equivalent to replaying the same
// physical keys in order. Merge, associativity, and appendFrom are checked
// against a naive sequential accumulator.

#include <vector>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Effort/RunningEffort.h"
#include "Keyboard/Config.h"
#include "Keyboard/PhysicalKeys.h"
#include "Property/PropertyDomains.h"

using namespace std;

namespace {

constexpr double EFFORT_TOLERANCE = 1e-9;

class EffortCompositionGeneratedPropertyTest {
 public:
  void MergeEqualsSequentialAppend(
      const vector<int>& aIds, const vector<int>& bIds) {
    expectMergeEqualsNaive(
        PropertyDomains::toPhysicalKeys(aIds),
        PropertyDomains::toPhysicalKeys(bIds));
  }

  void AssociativityMatchesSequentialAppend(
      const vector<int>& aIds,
      const vector<int>& bIds,
      const vector<int>& cIds) {
    expectAssociativeMergeEqualsNaive(
        PropertyDomains::toPhysicalKeys(aIds),
        PropertyDomains::toPhysicalKeys(bIds),
        PropertyDomains::toPhysicalKeys(cIds));
  }

  void AppendFromMatchesMergeAndSequentialAppend(
      const vector<int>& aIds, const vector<int>& bIds) {
    PhysicalKeys aKeys = PropertyDomains::toPhysicalKeys(aIds);
    PhysicalKeys bKeys = PropertyDomains::toPhysicalKeys(bIds);

    RunningEffort a = buildEffort(aKeys);
    RunningEffort b = buildEffort(bKeys);
    RunningEffort appended = a;
    appended.appendFrom(b, qwerty_);
    RunningEffort merged = RunningEffort::merge(a, b);
    RunningEffort naive = buildNaive(aKeys, bKeys);

    EXPECT_NEAR(
        naive.getEffort(qwerty_), appended.getEffort(qwerty_),
        EFFORT_TOLERANCE);
    EXPECT_NEAR(
        naive.getEffort(qwerty_), merged.getEffort(qwerty_),
        EFFORT_TOLERANCE);
    EXPECT_EQ(naive.getStrokes(), appended.getStrokes());
    EXPECT_EQ(naive.getStrokes(), merged.getStrokes());
  }

 private:
  Config qwerty_ = Config::qwerty();

  RunningEffort buildEffort(const PhysicalKeys& keys) {
    RunningEffort e;
    e.append(keys, qwerty_);
    return e;
  }

  RunningEffort buildNaive(const PhysicalKeys& a, const PhysicalKeys& b) {
    RunningEffort e;
    e.append(a, qwerty_);
    e.append(b, qwerty_);
    return e;
  }

  void expectMergeEqualsNaive(
      const PhysicalKeys& aKeys, const PhysicalKeys& bKeys) {
    RunningEffort naive = buildNaive(aKeys, bKeys);
    RunningEffort merged =
        RunningEffort::merge(buildEffort(aKeys), buildEffort(bKeys));

    EXPECT_NEAR(
        naive.getEffort(qwerty_), merged.getEffort(qwerty_),
        EFFORT_TOLERANCE)
        << "Merged effort should equal naive sequential effort";
    EXPECT_EQ(naive.getStrokes(), merged.getStrokes())
        << "Stroke counts should match";
  }

  // Ordered key streams are not commutative, so compare associativity to the
  // sequential baseline.
  void expectAssociativeMergeEqualsNaive(
      const PhysicalKeys& aKeys,
      const PhysicalKeys& bKeys,
      const PhysicalKeys& cKeys) {
    RunningEffort a = buildEffort(aKeys);
    RunningEffort b = buildEffort(bKeys);
    RunningEffort c = buildEffort(cKeys);

    RunningEffort left = RunningEffort::merge(RunningEffort::merge(a, b), c);
    RunningEffort right = RunningEffort::merge(a, RunningEffort::merge(b, c));

    RunningEffort naive;
    naive.append(aKeys, qwerty_);
    naive.append(bKeys, qwerty_);
    naive.append(cKeys, qwerty_);

    EXPECT_NEAR(
        naive.getEffort(qwerty_), left.getEffort(qwerty_),
        EFFORT_TOLERANCE);
    EXPECT_NEAR(
        naive.getEffort(qwerty_), right.getEffort(qwerty_),
        EFFORT_TOLERANCE);
    EXPECT_EQ(naive.getStrokes(), left.getStrokes());
    EXPECT_EQ(naive.getStrokes(), right.getStrokes());
  }
};

}  // namespace

FUZZ_TEST_F(EffortCompositionGeneratedPropertyTest, MergeEqualsSequentialAppend)
    .WithDomains(
        PropertyDomains::KeyIdsDomain(0, 16),
        PropertyDomains::KeyIdsDomain(0, 16));

FUZZ_TEST_F(
    EffortCompositionGeneratedPropertyTest, AssociativityMatchesSequentialAppend)
    .WithDomains(
        PropertyDomains::KeyIdsDomain(0, 12),
        PropertyDomains::KeyIdsDomain(0, 12),
        PropertyDomains::KeyIdsDomain(0, 12));

FUZZ_TEST_F(
    EffortCompositionGeneratedPropertyTest,
    AppendFromMatchesMergeAndSequentialAppend)
    .WithDomains(
        PropertyDomains::KeyIdsDomain(0, 16),
        PropertyDomains::KeyIdsDomain(0, 16));
