# Testing Guide

## Test Binaries

Core test binaries for different purposes:

| Binary | Purpose | Location |
|--------|---------|----------|
| `vimficiency_tests` | Unit tests (correctness) | `build/tests/vimficiency_tests` |
| `vimficiency_benchmarks` | Performance benchmarks | `build/tests/vimficiency_benchmarks` |
| `vimficiency_debug` | Scratch tests for debugging | `build/tests/vimficiency_debug` |
| `vimficiency_fuzz_tests` | Optional FuzzTest property/fuzz tests | `build-fuzztest/tests/vimficiency_fuzz_tests` |
| `vimficiency_raw_fuzzers` | Optional raw libFuzzer parser/FFI targets | `build-raw-fuzz/tests/` |

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

# Configure and run optional FuzzTest tests in short gtest-compatible mode
cmake -S . -B build-fuzztest -DVIMF_ENABLE_FUZZTEST=ON
cmake --build build-fuzztest -j --target vimficiency_fuzz_tests
./build-fuzztest/tests/vimficiency_fuzz_tests --gtest_brief=1

# Configure and run optional raw libFuzzer byte-level targets
cmake -S . -B build-raw-fuzz -DVIMF_ENABLE_LIBFUZZER=ON -DCMAKE_CXX_COMPILER=clang++
cmake --build build-raw-fuzz -j --target vimficiency_raw_fuzzers
env LSAN_OPTIONS=detect_leaks=0 ./build-raw-fuzz/tests/vimficiency_fuzz_payload -max_total_time=10
env LSAN_OPTIONS=detect_leaks=0 ./build-raw-fuzz/tests/vimficiency_fuzz_sequence_parser -max_total_time=10
env LSAN_OPTIONS=detect_leaks=0 ./build-raw-fuzz/tests/vimficiency_fuzz_movement_parser -max_total_time=10
env LSAN_OPTIONS=detect_leaks=0 ./build-raw-fuzz/tests/vimficiency_fuzz_edit_parser -max_total_time=10
env LSAN_OPTIONS=detect_leaks=0 ./build-raw-fuzz/tests/vimficiency_fuzz_snapshot -max_total_time=10

```

`VIMF_ENABLE_FUZZTEST` is off by default so the normal correctness suite
does not fetch/build the fuzzing stack or require a coverage/sanitizer-capable
toolchain. The gtest-compatible mode is short and prints `FUZZTEST_PRNG_SEED`
values for replay when it finds a failure.

FuzzTest's full coverage-guided modes require a Clang/sanitizer toolchain and
are intentionally manual. Do not add them to the default suite or CTest
discovery without first verifying the exact local toolchain; add raw libFuzzer
targets for byte-level parser campaigns if FuzzTest's native fuzzing mode is
not stable on that toolchain.

`VIMF_ENABLE_LIBFUZZER` is also off by default. Raw fuzzers are one executable
per byte boundary, built with Clang/libFuzzer plus ASan/UBSan instrumentation,
and are not CTest-discovered. They should stay narrow: payload decoders,
fallible sequence/edit parsers, snapshot decoding, and other APIs where
arbitrary bytes can cleanly return success or a structured error. Keep
assertions for impossible internal states; expose fallible APIs at file,
parser, and FFI boundaries before adding raw-byte fuzz coverage there.

The `LSAN_OPTIONS` prefix keeps smoke runs working in ptrace/sandboxed runners
where LeakSanitizer can fail after libFuzzer has already completed the inputs.
Omit it in a normal terminal when leak detection itself is the campaign goal.

## What Changed In The Test Cleanup

The suite now has sharper ownership by test type. When adding or moving tests,
choose the smallest category that proves the behavior:

| Test type | Use for | Developer rule |
|-----------|---------|----------------|
| Unit/assert tests | Exact local rules: ranges, cost math, parser errors, config, small helpers | Keep expected values explicit and deterministic. |
| Oracle conformance tests | VimCore commands/operators that must match Neovim | Use `NeovimOracle`; include cursor and mode when they are part of behavior. |
| Optimizer replay tests | Recommendations that claim to transform start -> goal | Verify emitted sequences replay to the goal, not just that a result exists. |
| Generated/property tests | Repeated invariant checks over generated valid states | Use named seeds and `GeneratedProperty::check`; generated failures must print seed and case. |
| FuzzTest tests | Structured generated parser/property coverage in gtest-compatible mode | Keep behind `VIMF_ENABLE_FUZZTEST`; do not depend on normal CI running it. |
| Raw libFuzzer targets | Arbitrary hostile bytes at parser/file/FFI boundaries | Keep behind `VIMF_ENABLE_LIBFUZZER`; target only fallible boundary APIs. |
| Golden tests | Curated user-facing examples and normalized reports | Keep small; update expected text only after human review. |
| Debug/manual investigations | Scratch repros, noisy traces, human approval exploration | Keep in `vimficiency_debug` or disabled tests; do not treat them as correctness coverage. |

Step 1 removed or quarantined misleading tests. Passing tests must assert the
thing they claim to assert. Human-approval and debug-style files are allowed
for investigation, but correctness tests should not rely on TODO assertions,
raw `cout`/`cerr` inspection, or counters that can pass without checking the
semantic condition.

Step 2 centralized replay checks. Use `OracleReplay::matches(...)` from
`tests/Utils/OracleReplay.h` when a sequence should reproduce an exact buffer,
cursor, and mode. Use `OptimizerResultChecks::expectTopResultsReplay(...)` from
`tests/Utils/OptimizerResultChecks.h` when optimizer results should be replayed
against Neovim. This keeps diagnostics consistent and avoids each optimizer
test hand-rolling a weaker replay check.

Step 3 promoted random loops into generated-property suites. Use
`GeneratedProperty::check({name, seed, iterations}, fn)` from
`tests/Utils/GeneratedProperty.h` for deterministic generated cases. Prefer
properties such as round trips, sorted costs, no duplicate results, and
candidate replay validity. Do not generate arbitrary start/end pairs for
optimizer tests; generate valid starts and valid edit scripts, derive the goal,
then verify invariants.

Step 4 added two opt-in fuzzing paths. `vimficiency_fuzz_tests` is the
structured FuzzTest/GTest-compatible binary. `vimficiency_raw_fuzzers` is the
raw libFuzzer path for arbitrary bytes. Raw fuzz targets should exercise APIs
like payload decoders, fallible sequence/edit parsers, and snapshot parsing.
If an API currently asserts on malformed input, first expose a fallible boundary
parser and keep assertions at trusted internal call sites.

Step 7 added minimal golden fixtures under `tests/Golden/`. These tests compare
normalized Explore recommendation reports against checked-in text fixtures. They
are intentionally small and should be curated by a human before being treated as
canonical product examples.

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

Two entry-point files are deliberately isolated:

- `simulate/integration.lua` — async coroutines + tab/window state. Keep its
  grouped cases in `tests/lua/simulate/_integration_cases_*.lua`; underscore
  files are loaded by the isolated runner and skipped by normal discovery.
- `capture/on_key_mapping_probe.lua` — Neovim characterization test
  that primes internal key-encoding state in ways downstream tests
  depend on NOT having happened (the `typed ~= key` heuristic in
  `key_tracking`). Documented in `run.sh`.

Explore flow tests are split by behavior family under
`tests/lua/explore/flow_*.lua`: motion, insert, restore/recovery, and
transform behavior. Shared mechanics belong in `tests/lua/explore/_helpers.lua`.

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
│   ├── MiscMotions.cpp
│   ├── CharFindMotions.cpp
│   ├── ScrollMotions.cpp
│   ├── CountedMiscMotions.cpp
│   └── MiscMotionsTestHelpers.h
├── Operator/          # VimCore edit/delete correctness (vs Neovim)
│   ├── Words.cpp
│   ├── Lines.cpp
│   ├── Sentences.cpp
│   ├── Paragraphs.cpp
│   ├── TextObjects.cpp
│   └── TestHelpers.cpp  # Shared helpers (in test_utils lib)
├── NavOptimizer/      # Optimizer output quality and correctness
│   ├── ManualTest.cpp
│   ├── BoundaryTest.cpp
│   ├── CountRepeatTest.cpp
│   ├── ManualTestHelpers.h
│   ├── OutputCorrectnessTest.cpp
│   ├── CostConsistencyTest.cpp
│   ├── DeterminismTest.cpp
│   └── HumanApprovalTest.cpp
├── TransformOptimizer/
│   ├── ManualTest.cpp
│   ├── AutoindentManualTest.cpp
│   ├── ExclusiveLinewiseManualTest.cpp
│   ├── ManualTestHelpers.h
│   ├── OutputCorrectnessTest.cpp
│   └── RegressionTests.cpp
├── CompositionOptimizer/
│   ├── ManualTest.cpp
│   ├── TextObjectManualTest.cpp
│   ├── PureInsertionManualTest.cpp
│   ├── JoinLinesManualTest.cpp
│   ├── ManualTestHelpers.h
│   ├── DiffStateTest.cpp
│   └── OutputCorrectnessTest.cpp
├── Benchmarks/        # Performance benchmarks (separate binary)
│   ├── BenchUtils.h   # Shared timing/output utilities
│   └── NavOptimizerBench.cpp
├── Misc/              # Catch-all for other tests
├── Explore/           # Explore state machine and recommendations
│   ├── ExploreTest.cpp
│   ├── RecommendationTest.cpp
│   ├── NativeFlowTest.cpp
│   ├── HeaderSpanTest.cpp
│   └── TestHelpers.h
├── Exploration/       # Opt-in dashboard trace collector
│   ├── ExplorationCollector.cpp
│   ├── ExplorationJsonWriter.cpp
│   ├── CompositionJsonWriter.cpp
│   ├── MotionCases.cpp
│   ├── EditCases.cpp
│   └── CompositionCases.cpp
├── Golden/            # Curated user-facing report fixtures
├── Utils/             # Shared test infrastructure (built as static library)
│   ├── NeovimOracle.cpp    # Neovim ground truth
│   ├── TestUtils.cpp
│   ├── EditTestGenerators.cpp
│   ├── GeneratedProperty.h
│   ├── OptimizerResultChecks.h
│   ├── OracleReplay.cpp
│   ├── OracleReplay.h
│   ├── RandomBufferHelpers.h
│   ├── RandomGeneration.h  # RandomGen singleton
│   ├── SeedManager.h       # Seed mode management
│   └── SeedManager.cpp
├── Fuzz/              # Optional FuzzTest suites and raw libFuzzer targets
└── Debug.cpp          # Scratchpad for debugging (separate binary)
```

## Test Categories

| Category | Purpose | Ground Truth |
|----------|---------|--------------|
| Commands/ | Verify VimCore motions match Neovim | NeovimOracle |
| Operator/ | Verify VimCore edits match Neovim | NeovimOracle |
| *Optimizer/ | Verify optimizer outputs are correct and reproducible | Replay helpers + focused unit checks |
| Golden/ | Protect curated user-facing report examples | Human-reviewed fixtures |
| Fuzz/ | Verify parser/FFI boundaries reject malformed generated input cleanly | Invariants + clean rejection |

### Where New Tests Belong

Choose the test location by the proof obligation first, then by subsystem:

- A single helper, parser rule, cost rule, or data-structure invariant belongs
  in the closest existing unit/assert file for that module.
- Vim command semantics belong in `Commands/` or `Operator/` with
  `NeovimOracle` as ground truth.
- Optimizer recommendations belong in the relevant `*Optimizer/` suite, using
  replay helpers when the result claims to transform text or reach a cursor.
- Repeated checks over generated valid states belong in a named generated
  property test, using `GeneratedProperty::check`.
- Arbitrary hostile bytes belong in `Fuzz/`, and only against fallible boundary
  APIs that can cleanly accept or reject malformed input.
- Human-facing report examples belong in `Golden/`, with expected output
  updated only after review.
- Lua plugin behavior belongs under `tests/lua/`, split by user-facing module
  or flow family; shared setup should move into a local helper.
- Scratch investigation belongs in `vimficiency_debug`, not in the correctness
  suite.

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

Each test should make its proof style obvious from the name and helper choice:

1. Manual examples: dense, specific regressions and edge cases.
2. Oracle conformance: Neovim-backed checks for VimCore command semantics.
3. Replay correctness: optimizer candidates must execute to the requested goal.
4. Generated properties: deterministic loops over valid generated cases.
5. Fuzz boundaries: opt-in arbitrary byte campaigns for fallible parsers.

```cpp
// Manual: specific edge case
TEST_F(WordMotionTest, Manual_EmptyLineIsWord) {
  Lines lines = {"hello", "", "world"};
  auto result = oracle->simulate(lines, 0, 4, "w");
  EXPECT_EQ(result.row, 1);  // Stops at empty line
}

// Generated property: invariant over many valid cases
TEST(DiffStateGeneratedPropertyTest, SingleLineRoundTripAndStructure) {
  GeneratedProperty::check({"DiffState single-line round-trip", 42, 100}, [&](int) {
    Lines initial = {randomLine(RandomGen::range(5, 30))};
    Lines goal = randomlyEdit(initial);
    if (initial == goal) return;

    auto diffs = Myers::calculate(initial, goal);
    EXPECT_EQ(Myers::applyAllDiffState(diffs, initial), goal);
    validateInvariants(diffs, initial, goal);
  });
}
```

### When to Add Manual Tests
- Regression test for a fixed bug
- Document tricky expected behavior
- Specific buffer structures random generation won't produce

### When to Add Generated Tests
- The expected answer is an invariant, not one literal output.
- The input generator can produce valid states cheaply.
- A failing case can be replayed from seed and case index.
- The loop can stay fast enough for the normal `vimficiency_tests` binary.

### When to Add Fuzz Targets
- The input is naturally bytes from Lua, files, snapshots, or parser text.
- Bad input can return a structured error without assertion failure.
- The target is deterministic, narrow, and does not spawn Neovim.
- The target can run with ASan/UBSan in a dedicated Clang build.

## Debugging

- Use `tests/Debug/Debug.cpp` for scratchpad debugging (separate binary: `./build/tests/vimficiency_debug`)
- Use `debug()` macro from `Utils/Debug.h` (enabled by default via `VIMFICIENCY_DEBUG`)
- Use `SequenceTracer` to step through motions; see `dev/code-semantics.md` for cursor and `targetCol` invariants

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
