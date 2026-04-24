# Testing Guide

## Test Binaries

Three separate test binaries for different purposes:

| Binary | Purpose | Location |
|--------|---------|----------|
| `vimficiency_tests` | Unit tests (correctness) | `build/tests/vimficiency_tests` |
| `vimficiency_benchmarks` | Performance benchmarks | `build/tests/vimficiency_benchmarks` |
| `vimficiency_debug` | Scratch tests for debugging | `build/tests/vimficiency_debug` |

### Running Tests

```bash
# Run all unit tests
./build/tests/vimficiency_tests --gtest_brief=1

# Run specific test suite
./build/tests/vimficiency_tests --gtest_filter="WordMotionTest.*"

# Run specific test
./build/tests/vimficiency_tests --gtest_filter="*Manual_EmptyLineIsWord"

# List all tests without running
./build/tests/vimficiency_tests --gtest_list_tests

# Run all benchmarks
./build/tests/vimficiency_benchmarks

# Run specific benchmark
./build/tests/vimficiency_benchmarks --gtest_filter="*BufferSize"

# Run debug tests
./build/tests/vimficiency_debug
```

### Lua Tests

The Lua layer has its own harness separate from gtest, driven by a
hand-rolled runner at `tests/lua/runner.lua`.

```bash
# Run the whole Lua suite (single headless Neovim for the main batch;
# two escape-hatch files in their own processes — see run.sh).
bash tests/lua/run.sh

# Run a single Lua test file for focused iteration.
VF_TEST_FILE=tests/lua/session/store_invariants.lua \
  nvim --headless -u NONE -U NONE -l tests/lua/runner.lua

# Run C++ and Lua suites together.
bash scripts/test.sh
```

The single-process design amortizes Neovim's ~150ms startup cost over
all test files. Between files, `reset_state()` in `runner.lua` tears
down the state our plugin mutates (on_key subscribers, augroup,
`:Vimfy` command, scratch buffers, plugin `package.loaded` entries,
`XDG_DATA_HOME`). If a new stateful module lands and tests start
flaking, look there first — prefer extending the reset over reverting
to per-file processes.

Two files are deliberately isolated:

- `simulate/integration.lua` — async coroutines + tab/window state.
- `capture/on_key_mapping_probe.lua` — Neovim characterization test
  that primes internal key-encoding state in ways downstream tests
  depend on NOT having happened (the `typed ~= key` heuristic in
  `key_tracking`). Documented in `run.sh`.

### Using `vimficiency_debug` For Codegen Checks

`vimficiency_debug` is also the right place for scratch code used to inspect optimizer code generation in a Release build.

For search stats, the important distinction is:

- compile-time-disabled helpers can compile away completely
- runtime-disabled helpers still leave a branch in the hot path

The `DebugTest.DISABLED_SearchStatsCodegen` scratch test keeps dedicated stats hot-loop symbols in the binary so you can inspect them with:

```bash
cmake --build build -j --target vimficiency_debug
nm -C build/tests/vimficiency_debug | rg 'searchStatsHotLoop'
llvm-objdump -d -C build/tests/vimficiency_debug | rg -A40 'searchStatsHotLoop<false>|searchStatsHotLoop<true>'
```

That workflow is better than reasoning abstractly about whether Clang removed a stats call, because it checks the actual optimized binary we ship locally.

One important caveat: a `debug(...)`-style helper does not prevent argument evaluation. If the call site eagerly builds a `std::string`, that work still happens before the helper is entered. For expensive trace payloads, keep the construction lazy so the compile-time-disabled path removes both the helper body and the payload construction.

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
├── NavOptimizer/   # Optimizer output quality and correctness
│   ├── OutputCorrectnessTest.cpp
│   ├── CostConsistencyTest.cpp
│   ├── DeterminismTest.cpp
│   └── HumanApprovalTest.cpp
├── EditOptimizer/     # Same structure as NavOptimizer
├── CompositionOptimizer/
├── Benchmarks/        # Performance benchmarks (separate binary)
│   ├── BenchUtils.h   # Shared timing/output utilities
│   └── NavOptimizerBench.cpp
├── Misc/              # Catch-all for other tests
├── Utils/             # Shared test infrastructure (built as static library)
│   ├── NeovimOracle.cpp    # Neovim ground truth
│   ├── TestUtils.cpp
│   ├── EditTestGenerators.cpp
│   ├── RandomBufferHelpers.h
│   ├── RandomGeneration.h  # RandomGen singleton
│   ├── SeedManager.h       # Seed mode management
│   └── SeedManager.cpp
└── Debug.cpp          # Scratchpad for debugging (separate binary)
```

## Test Categories

| Category | Purpose | Ground Truth |
|----------|---------|--------------|
| Commands/ | Verify VimCore motions match Neovim | NeovimOracle |
| Operator/ | Verify VimCore edits match Neovim | NeovimOracle |
| *Optimizer/ | Verify optimizer outputs are correct and reproducible | Simulation + manual |

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
VIMFICIENCY_SEED_MODE=fixed ./build/tests/vimficiency_benchmarks

# Replay last run's seeds
VIMFICIENCY_SEED_MODE=replay ./build/tests/vimficiency_benchmarks

# Random seeds (default, explicit)
VIMFICIENCY_SEED_MODE=random ./build/tests/vimficiency_benchmarks

# Custom seed file
VIMFICIENCY_SEED_FILE=my_seeds.txt VIMFICIENCY_SEED_MODE=replay ./build/tests/vimficiency_benchmarks
```

### Reproducing Failures

When a test fails with random seeds:

1. Seeds are already logged in `tests/.last_seeds.txt`
2. Replay with: `VIMFICIENCY_SEED_MODE=replay ./build/tests/vimficiency_tests`
3. Once reproduced, switch to fixed mode for debugging: `SeedManager::instance().setFixedMode()`

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
  RandomGen::seed(42);  // Fixed seed for reproducibility
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

- Use `tests/Debug.cpp` for scratchpad debugging (separate binary: `./build/tests/vimficiency_debug`)
- Use `debug()` macro from `Utils/Debug.h` (enabled by default via `VIMFICIENCY_DEBUG`)
- Use `SequenceTracer` to step through motions (see `vim-utils-principles.md` §5)

## Vim Documentation Reference

For implementing or verifying VimCore behavior:
- `dev/vim/motion.txt` - Motion commands
- `dev/vim/change.txt` - Change operators
- `dev/vim/index.txt` - Command index

## Test Data Files

Files in `data/TestFiles/` for Optimizer testing:
- `a*` prefix: Abstract cases (long lines, block lines, spaced lines)
- `m*` prefix: Realistic code snippets

Load with `TestFiles::load("a1_long_line.txt")`.
