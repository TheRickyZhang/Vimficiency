# RapidCheck Adoption Evaluation

## What is Property-Based Testing?

Instead of writing specific test cases:
```cpp
// Example-based: "does w work for this specific buffer?"
TEST(Motion, w_specific) {
  Lines lines = {"hello world"};
  Position result = applyMotion({0, 0}, "w", lines);
  EXPECT_EQ(result.col, 6);
}
```

You declare properties that should hold for *all* inputs:
```cpp
// Property-based: "w always matches Neovim for any buffer"
RC_GTEST_PROP(Motion, w_matches_neovim, (Lines lines, Position start)) {
  Position ours = applyMotion(start, "w", lines);
  auto expected = oracle.simulate(lines, start, "w");
  RC_ASSERT(ours == expected);
}
```

The framework then:
1. Generates random inputs (100+ cases by default)
2. If a failure is found, **shrinks** to the minimal failing case
3. Reports coverage statistics

## Current State: You Already Do Property-Based Testing

Your codebase implements the pattern manually:
```cpp
// tests/Commands/WordMotions.cpp - your current approach
void testMotionRandom(const string& motion, int iterations) {
  RandomGen::seed(42);
  for (int i = 0; i < iterations; i++) {
    auto test = generateRandomMotionBuffer(3);
    Position ours = simulateMotions(test.start, motion, test.lines);
    auto expected = oracle->simulate(...);
    EXPECT_EQ(ours, expected);
  }
}
```

This IS property-based testing. You're checking "motion X matches Neovim for all random buffers."

**What you're missing**: automatic shrinking and coverage feedback.

## The Shrinking Problem

When a test fails with a 50-line buffer, you get:
```
FAILURE: Motion w mismatch
Buffer: {"the quick brown fox...", "jumped over the...", ...50 more lines...}
Start: (23, 47)
```

You must manually find the minimal reproducing case. With RapidCheck:
```
FAILURE: Motion w mismatch
Shrunk to minimal case after 47 attempts:
Buffer: {"a ", ""}
Start: (0, 1)
```

The minimal case often reveals the bug instantly.

## The NeovimOracle Blocker

Your highest-value tests use NeovimOracle for ground truth. Problem:

```cpp
// Oracle has stability limits (from docs/testing.md)
// "After ~800 buffer operations, call oracle->restart() to reset"
```

RapidCheck shrinking might call the oracle 100+ times for a single failure. With the 800-op limit, this risks crashes mid-shrink.

**This is the main reason to wait.**

## Alignment Assessment

| Your Testing Pattern | RapidCheck Feature | Alignment |
|---------------------|-------------------|-----------|
| Random buffers with seeds | `Arbitrary<Lines>` generators | High |
| Oracle comparison | Properties with `RC_ASSERT` | High |
| Iteration counts (30, 50, 100) | Configurable `numTests` | High |
| Structured generators (prose, code) | Custom `Gen<T>` combinators | Medium |
| NeovimOracle ground truth | External oracle integration | **Low** (stability issue) |

**Your testing style aligns well with property-based testing conceptually.** The friction is purely the oracle stability, not a paradigm mismatch.

## Practical Recommendation for Solo Developer

### Option 1: Partial Adoption (Recommended if curious)

Add RapidCheck for tests that **don't** need NeovimOracle:
- `CostConsistencyTest` - property: computed cost == reported cost
- `DeterminismTest` - property: same input → same output
- `HashCollisionTest` - property: hash function is stable

Effort: ~4 hours. Gets you familiar with the syntax. Low risk.

```cpp
// Example conversion
RC_GTEST_PROP(CostConsistency, cost_matches_computed, ()) {
  Lines lines = *rc::gen::arbitrary<Lines>();
  Position start = *rc::gen::arbitrary<Position>();
  Position end = *rc::gen::arbitrary<Position>();

  auto results = optimizer.optimize(lines, start, end);
  for (const auto& r : results) {
    RC_ASSERT(computeCost(r.sequence) == r.keyCost);
  }
}
```

### Option 2: Wait and Revisit

If debugging test failures isn't currently painful, the ROI is low. Your existing infrastructure works. Revisit when:
- A bug takes >30 min to minimize manually
- You make NeovimOracle more robust
- You want to expand test coverage significantly

### Option 3: Manual Shrinking (DIY)

Add shrinking to your existing infrastructure without RapidCheck:
```cpp
// In TestUtils.h
template<typename Predicate>
Lines shrinkBuffer(const Lines& failing, Predicate stillFails) {
  Lines minimal = failing;

  // Try removing lines
  for (size_t i = 0; i < minimal.size(); ) {
    Lines candidate = minimal;
    candidate.erase(candidate.begin() + i);
    if (!candidate.empty() && stillFails(candidate)) {
      minimal = candidate;
    } else {
      i++;
    }
  }

  // Try shortening lines
  for (size_t i = 0; i < minimal.size(); i++) {
    while (minimal[i].size() > 1) {
      Lines candidate = minimal;
      candidate[i].pop_back();
      if (stillFails(candidate)) {
        minimal = candidate;
      } else {
        break;
      }
    }
  }

  return minimal;
}
```

This gives you shrinking without a new dependency.

## Learning RapidCheck Syntax

If you want to learn the idioms anyway (useful knowledge), the core patterns are:

```cpp
// 1. Basic property
RC_GTEST_PROP(Suite, name, (Type1 arg1, Type2 arg2)) {
  RC_ASSERT(property(arg1, arg2));
}

// 2. Custom generator
auto smallBuffer = rc::gen::container<Lines>(
  rc::gen::inRange(1, 5),  // 1-5 lines
  rc::gen::string<std::string>()
);

// 3. Preconditions (skip invalid inputs)
RC_GTEST_PROP(Suite, name, (Lines lines, Position pos)) {
  RC_PRE(pos.line < lines.size());  // Skip if invalid
  RC_PRE(pos.col < lines[pos.line].size());
  // ... test
}

// 4. Classifications (coverage insight)
RC_GTEST_PROP(Suite, name, (Lines lines)) {
  RC_CLASSIFY(lines.size() == 1, "single line");
  RC_CLASSIFY(lines.size() > 10, "large buffer");
  // ... test
}
```

## Files That Would Change

| File | Changes |
|------|---------|
| `CMakeLists.txt` | `FetchContent(rapidcheck)` |
| `tests/CMakeLists.txt` | Link rapidcheck |
| `tests/Utils/RapidCheckGenerators.h` | `Arbitrary<Lines>`, `Arbitrary<Position>` |
| Converted test files | Replace loops with `RC_GTEST_PROP` |

## Verdict

| Factor | Assessment |
|--------|------------|
| Conceptual alignment | High - you already think in properties |
| Oracle compatibility | Low - stability limits hurt shrinking |
| Learning investment | Medium - new syntax, but transferable skill |
| Current pain level | Low - tests work, debugging is manageable |
| **Recommendation** | Try partial adoption OR wait |

**Bottom line**: Your testing style is already property-based. RapidCheck would formalize it and add shrinking. The oracle stability issue means full adoption has friction. Try it on non-oracle tests first to see if you like the syntax, then decide.
