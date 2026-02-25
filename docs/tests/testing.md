# Testing Guide

## Test Binaries

Primary binaries/tools used in test workflows:

| Binary | Purpose | Location |
|--------|---------|----------|
| `vimficiency_tests` | Unit tests (correctness) | `build/tests/vimficiency_tests` |
| `vimficiency_benchmarks` | Performance benchmarks | `build/tests/vimficiency_benchmarks` |
| `vimficiency_debug` | Scratch tests for debugging | `build/tests/vimficiency_debug` |
| `vimficiency_explore` | Search-space data collection for dashboard | `build/tests/vimficiency_explore` |
| `vimficiency_diff_debug` | Standalone Myers diff visualizer | `build/tests/vimficiency_diff_debug` |

### Running Tests

```bash
# Run all unit tests
./build/tests/vimficiency_tests --gtest_brief=1

# Force random seed mode (overrides an inherited env var)
VIMFICIENCY_SEED_MODE=random ./build/tests/vimficiency_tests --gtest_brief=1

# Run specific test suite
./build/tests/vimficiency_tests --gtest_filter="WordMotionsTest.*"

# Run specific test
./build/tests/vimficiency_tests --gtest_filter="WordsTest.EdgeCase_EmptyLine"

# List all tests without running
./build/tests/vimficiency_tests --gtest_list_tests

# Run all benchmarks
./build/tests/vimficiency_benchmarks

# Run specific benchmark
./build/tests/vimficiency_benchmarks --benchmark_filter=".*BufferSize.*"

# Replay the last random-seed run
VIMFICIENCY_SEED_MODE=replay ./build/tests/vimficiency_tests --gtest_brief=1

# Run debug tests
./build/tests/vimficiency_debug
```

## Directory Structure

```
tests/
├── Commands/          # VimCore motion correctness (vs Neovim)
│   ├── WordMotions.cpp
│   ├── LineMotions.cpp
│   ├── SentenceMotions.cpp
│   ├── ParagraphMotions.cpp
│   ├── CountMotionsTest.cpp
│   └── MiscMotions.cpp
├── Operator/          # VimCore edit/delete correctness (vs Neovim)
│   ├── Words.cpp
│   ├── Lines.cpp
│   ├── Sentences.cpp
│   ├── Paragraphs.cpp
│   ├── TextObjects.cpp
│   └── TestHelpers.cpp  # Shared helpers (in test_utils lib)
├── MotionOptimizer/   # Optimizer output quality and correctness
│   ├── OutputCorrectnessTest.cpp
│   ├── CostConsistencyTest.cpp
│   ├── DeterminismTest.cpp
│   └── HumanApprovalTest.cpp
├── EditOptimizer/     # Same structure as MotionOptimizer
├── CompositionOptimizer/
├── Benchmarks/        # Performance benchmarks (separate binary)
│   ├── BenchUtils.h   # Shared timing/output utilities
│   └── MotionOptimizerBench.cpp
├── Misc/              # Catch-all for other tests
├── Utils/             # Shared test infrastructure (built as static library)
│   ├── NeovimOracle.cpp    # Neovim ground truth
│   ├── TestUtils.cpp
│   ├── EditTestGenerators.cpp
│   ├── RandomBufferHelpers.h
│   ├── RandomGeneration.h  # RandomGen singleton
│   ├── SeedManager.h       # Seed mode management
│   └── SeedManager.cpp
└── Debug/             # Debug scratchpad + historical investigation artifacts
```

## Test Categories

| Category | Purpose | Ground Truth |
|----------|---------|--------------|
| Commands/ | Verify VimCore motions match Neovim | NeovimOracle |
| Operator/ | Verify VimCore edits match Neovim | NeovimOracle |
| *Optimizer/ | Verify optimizer outputs are correct and reproducible | Simulation + manual |

## HumanApproval Tests

`*HumanApprovalTest.cpp` files are intentionally heuristic quality checks.

- Keep these tests enabled in `vimficiency_tests` for now.
- Mark non-objective checks with `TODO(HUMAN_APPROVAL_HARDEN)`.
- Prefer objective invariants (validity, goal reachability, bounded effort/ranking envelopes) when hardening.

Current audit targets:

- `tests/MotionOptimizer/HumanApprovalTest.cpp`
- `tests/EditOptimizer/HumanApprovalTest.cpp`
- `tests/CompositionOptimizer/HumanApprovalTest.cpp`

Audit helper:

```bash
scripts/check-human-approval-todos.sh
```

## Ground Truth: NeovimOracle

All VimCore behavior should match Neovim. Use `tests/Utils/NeovimOracle` to get expected output directly from an embedded Neovim process.

**Architecture**: Communicates via msgpack-RPC with `nvim --embed --headless`. Single process reused across test suite.

**Stability note**: After ~800 buffer operations, call `oracle->restart()` to reset.

### Setup Pattern
```cpp
class MyTest : public ::testing::Test {
protected:
  static std::unique_ptr<NeovimOracle> oracle;
  static void SetUpTestSuite() { oracle = std::make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle.reset(); }
};
std::unique_ptr<NeovimOracle> MyTest::oracle;

TEST_F(MyTest, Example) {
  Lines lines = {"hello", "world"};
  auto result = oracle->simulate(lines, 0, 0, "w");  // 0-indexed
  EXPECT_EQ(result.row, 0);
  EXPECT_EQ(result.col, 6);
}
```

## Seed Management

Tests and benchmarks use `SeedManager` for reproducible randomness. By default, seeds are **random** and logged to a file for replay if needed.

### Modes

| Mode | Behavior | Use Case |
|------|----------|----------|
| Random (default) | Generate random seeds, log to `tests/.last_seeds.txt` | Normal testing/benchmarks |
| Fixed | Deterministic seeds (42, 43, 44, ...) | Debugging, bisecting failures |
| Replay | Read seeds from log file | Reproduce a previous run |

### Seed Policy (Intentional Explicitness)

- Default test runs should not set `VIMFICIENCY_SEED_MODE=random`; random is already the default.
- Keep `VIMFICIENCY_SEED_MODE=random` only when you need to override an inherited env var (for example, a shell exported with fixed/replay mode).
- Keep `VIMFICIENCY_SEED_MODE=fixed` in benchmark/exploration workflows where cross-run and cross-commit comparability matters.
- In randomized correctness tests, prefer `TEST_SEED(index)` so mode switching (`random`, `fixed`, `replay`) works without code edits.
- Use literal seeds (for example, `RandomGen::seed(465950)`) only for pinned regressions or intentionally frozen random corpora, and include a short comment explaining why.

### Usage in Code

```cpp
#include "Utils/SeedManager.h"

// Get seeds (uses current mode)
int seed = SeedManager::instance().getSeed(0);  // First seed
auto seeds = SeedManager::instance().getSeeds(5);  // Multiple seeds

// Or use the macro
int seed = TEST_SEED(0);

// Switch to fixed mode for debugging
SeedManager::instance().setFixedMode();  // Uses 42, 43, 44, ...
SeedManager::instance().setFixedMode(100);  // Uses 100, 101, 102, ...

// Switch to replay mode
SeedManager::instance().setReplayMode();  // Uses tests/.last_seeds.txt
SeedManager::instance().setReplayMode("path/to/seeds.txt");  // Custom file

// Switch back to random
SeedManager::instance().setRandomMode();
```

### Usage via Environment Variables

For CI or scripts without code changes:

```bash
# Fixed seeds (for debugging)
VIMFICIENCY_SEED_MODE=fixed ./build/tests/vimficiency_tests

# Replay last run's seeds
VIMFICIENCY_SEED_MODE=replay ./build/tests/vimficiency_tests

# Random seeds (explicit override; default is already random)
VIMFICIENCY_SEED_MODE=random ./build/tests/vimficiency_tests

# Custom seed file
VIMFICIENCY_SEED_FILE=my_seeds.txt VIMFICIENCY_SEED_MODE=replay ./build/tests/vimficiency_tests
```

### Reproducing Failures

When a test fails with random seeds:

1. Seeds are logged in `tests/.last_seeds.txt` (local) or uploaded as CI artifact (`test-seeds`).
2. Replay locally with: `VIMFICIENCY_SEED_MODE=replay ./build/tests/vimficiency_tests`.
3. For CI failures, download the `test-seeds` artifact and run:
   `VIMFICIENCY_SEED_FILE=<downloaded-file> VIMFICIENCY_SEED_MODE=replay ./build/tests/vimficiency_tests`.
4. Once reproduced, optionally switch to fixed mode for stepwise debugging: `SeedManager::instance().setFixedMode()`.

### Benchmark Multi-Seed Averaging

Benchmarks run multiple iterations with different seeds and aggregate results:

```cpp
auto& seedMgr = SeedManager::instance();
for (int i = 0; i < seedCount; i++) {
  RandomGen::seed(seedMgr.getSeed(i));
  // ... run benchmark iteration
}
```

Results show aggregated stats with star markers (`*`) indicating how many runs hit each stop condition (e.g., `48*****` means all 5 runs hit that limit).

## Test Fixture Naming (ODR Violation Prevention)

**CRITICAL**: Test fixture names must NOT match any `struct`/`class` names in `src/`.

C++ ODR (One Definition Rule) violations occur when the same type name has different definitions in different translation units. This causes **silent memory corruption** that's extremely hard to debug - ASan sees corrupted memory but can't explain why.

### Naming Convention

Always suffix test fixtures with `Test`, `Tests`, `Bench`, or similar:

```cpp
// BAD - may collide with struct TextObjectContext in src/
class TextObjectContext : public ::testing::Test { };

// GOOD - clearly a test fixture
class TextObjectContextTest : public ::testing::Test { };
```

### Verification

Run this to check for potential collisions:

```bash
# Find test fixtures that might collide with production types
scripts/check-test-fixture-names.sh
```

This compares test fixture names against all struct/class names in `src/` and reports any exact matches.

## Test Writing Strategy

Each test file should have two sections:

1. **Manual cases** (top): Dense, specific scenarios for easy debugging
2. **Randomized stress tests** (bottom): Bulk coverage via NeovimOracle comparison

```cpp
// Manual: specific edge case
TEST_F(WordMotionTest, Manual_EmptyLineIsWord) {
  Lines lines = {"hello", "", "world"};
  auto result = oracle->simulate(lines, 0, 4, "w");
  EXPECT_EQ(result.row, 1);  // Stops at empty line
}

// Randomized: bulk coverage
TEST_F(WordMotionTest, Random_wMotion) {
  RandomGen::seed(TEST_SEED(0));  // Random-by-default, replayable via SeedManager
  for (int i = 0; i < 100; i++) {
    auto buffer = generateRandomBuffer(5);
    Position start = randomPosition(buffer);
    Position ours = applyMotion(start, "w", buffer);
    auto expected = oracle->simulate(buffer, start.line, start.col, "w");
    EXPECT_EQ(ours.line, expected.row) << "Iteration " << i;
    EXPECT_EQ(ours.col, expected.col) << "Iteration " << i;
  }
}
```

### When to Add Manual Tests
- Regression test for a fixed bug
- Document tricky expected behavior
- Specific buffer structures random generation won't produce

## Debugging

- Use `tests/Debug/Debug.cpp` for active scratchpad debugging (binary: `./build/tests/vimficiency_debug`)
- `tests/Debug/OldDebug.cpp` is historical but still compiled into `vimficiency_debug`
- `tests/Debug/Motion.cpp`, `tests/Debug/MiscFailures.cpp`, `tests/Debug/Diffs.cpp`, and `tests/Debug/Indent.cpp` are reference-only unless wired into CMake
- See `docs/tests/debug-taxonomy.md` for Active/Historical/Archive status
- Use `debug()` macro from `Utils/Debug.h` (enabled by default via `VIMFICIENCY_DEBUG`)
- Use `SequenceTracer` to step through motions (see `vim-utils-principles.md` §5)

## Vim Documentation Reference

For implementing or verifying VimCore behavior:
- `docs/vim/motion.txt` - Motion commands
- `docs/vim/change.txt` - Change operators
- `docs/vim/index.txt` - Command index

## Test Data Files

Files in `data/TestFiles/` for Optimizer testing:
- `a*` prefix: Abstract cases (long lines, block lines, spaced lines)
- `m*` prefix: Realistic code snippets

Load with `TestFiles::load("a1_long_line.txt")`.
