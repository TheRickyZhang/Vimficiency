# Testing Guide

## Test Methodology Model

The primary axis is *how* a test produces evidence, not what feature it covers.
Methodology determines the runner, runtime, compiler flags, and CI policy;
features (oracle conformance, optimizer replay, parser invariants, etc.) are a
sub-layer that show up *within* a methodology — sometimes under more than one,
when the methodologies prove different things about the same feature.

| Methodology | How it proves something | Runner | Default gate | Command |
|-------------|-------------------------|--------|--------------|---------|
| Unit/assert | Hand-written deterministic check of a fixed case | `vimficiency_tests` | Yes | `./build/tests/vimficiency_tests --gtest_filter="-*Golden*Test.*:*GeneratedProperty*"` |
| Property | Generated inputs check a universally-quantified invariant | `vimficiency_fuzz_tests` (seed-only mode) | Yes, seed-only | `env FUZZTEST_FUZZ_FOR=0 FUZZTEST_PRNG_SEED=OffQXb8u5_vZtH4-7wgVOLu_HNAhPIbLz7CFF13u3nk ./build/tests/vimficiency_fuzz_tests --gtest_filter="*GeneratedProperty*"` |
| Expect/golden | Curated output compared to a stored fixture after normalization | `vimficiency_tests` | Yes | `./build/tests/vimficiency_tests --gtest_filter="*Golden*Test.*"` |
| Fuzz | Coverage-guided exploration of an invariant on fallible inputs | `vimficiency_fuzz_tests` built with `FUZZTEST_FUZZING_MODE=ON` | No | `./build-fuzz/tests/vimficiency_fuzz_tests --fuzz=<TestName> --fuzz_for=30s` |
| Lua integration | Real-Neovim plugin/FFI scenarios | `tests/lua/run.sh` | Yes | `bash tests/lua/run.sh` |
| Benchmark | Timing and search-counter trends on fixed fixtures | `vimficiency_benchmarks` | No | `./build/tests/vimficiency_benchmarks` |
| Debug/manual | Scratch repros, noisy traces, human-approval exploration | `vimficiency_debug` or disabled tests | No | `./build/tests/vimficiency_debug` |

Unit/assert and expect/golden share the `vimficiency_tests` binary. The
unit/assert filter is the negative complement of golden + property — adequate
today, and the right shape if more methodologies ever migrate into this binary.
Property and fuzz share the `vimficiency_fuzz_tests` source files but differ in
build configuration and runtime budget (see [Why Properties And Fuzzing Share
A Runner](#why-properties-and-fuzzing-share-a-runner)).

### What each methodology covers

Feature areas usually map to one methodology. Where a feature appears under
more than one, the methodologies are checking different things about it (e.g.
unit/assert pins regressions; property checks the universal invariant).

- **Unit/assert** — named-bug regressions, canonical-semantic documentation,
  and specific-output assertions for VimCore commands and operators
  (`tests/Commands/`, `tests/Operator/`); fixed-case optimizer replay
  regressions and cost/heuristic/determinism checks
  (`tests/NavOptimizer/`, `tests/TransformOptimizer/`,
  `tests/CompositionOptimizer/`, `tests/Optimizer/`); parser error cases, cost
  math, range and position helpers, config defaults, data-structure invariants
  (`tests/Misc/`); explore-flow tests (`tests/Explore/`). Bare oracle
  conformance (ours == oracle on hand-picked inputs) lives under `Properties/`
  instead.
- **Property** — oracle conformance over generated motion/operator inputs,
  optimizer replay over generated edit/nav problems, structural invariants of
  parsed types, effort-model identities (`tests/Properties/`).
- **Expect/golden** — user-facing report rendering and stable serialization
  (`tests/Golden/`).
- **Fuzz** — fallible parser, payload, snapshot, and FFI-boundary inputs that
  must accept-or-reject cleanly (`tests/Fuzz/`).
- **Lua integration** — plugin lifecycle, session storage, view flows, FFI
  smoke, key-capture probes (`tests/lua/`).
- **Benchmark** — optimizer time/space/search-counter trends on fixed fixture
  seeds (`tests/Benchmarks/`).
- **Debug/manual** — scratch investigation; not gated.

Google FuzzTest is the canonical framework for both generated property tests and
fuzzing. That gives us one mature registration/domain/shrinking/corpus tool.

### Determinism Policy

Property tests are deterministic in CI. CI runs the FuzzTest binary with:

```bash
env FUZZTEST_FUZZ_FOR=0 \
  FUZZTEST_PRNG_SEED=OffQXb8u5_vZtH4-7wgVOLu_HNAhPIbLz7CFF13u3nk \
  ./build/tests/vimficiency_fuzz_tests --gtest_brief=1
```

`FUZZTEST_FUZZ_FOR=0` means "run explicit `.WithSeeds(...)` inputs only." For
the migrated generated-property suites, those seeds replay the same deterministic
case corpus the old custom helper covered. With the current FuzzTest engine,
the runner still reports about one second per `FUZZ_TEST`; that is runner
overhead, not a license to treat CI as a fuzz campaign.

CTest-discovered FuzzTest cases are registered with the same environment.

For normal confidence checks, use the same seed-only command as CI. When you
are intentionally hunting for new failures, omit `FUZZTEST_FUZZ_FOR=0` and let
FuzzTest's default unit-test exploration run. It prints `FUZZTEST_PRNG_SEED` for
replay:

```bash
./build/tests/vimficiency_fuzz_tests --gtest_filter="DiffStateGeneratedPropertyTest.*"

env FUZZTEST_PRNG_SEED=<printed-seed> \
  ./build/tests/vimficiency_fuzz_tests --gtest_filter="DiffStateGeneratedPropertyTest.*"
```

When exploratory property testing finds a real bug, add the reduced/concrete
case as a manual regression test or a checked-in FuzzTest seed before relying on
random exploration to find it again.

### Why Properties And Fuzzing Share A Runner

Property tests and fuzz campaigns use the same `FUZZ_TEST` declarations and
domains. The difference is the mode:
- Unit-test mode: gtest-compatible, short local exploration, seed-only in CI.
- Fuzzing mode: coverage-guided campaign, one selected fuzz test, sanitizer and
  coverage flags, manual runtime budget.

## Test Runners

| Binary/script | Purpose | Location | Run command |
|---------------|---------|----------|-------------|
| `vimficiency_tests` | Deterministic C++ correctness: unit, oracle, replay, golden | `build/tests/vimficiency_tests` | `./build/tests/vimficiency_tests --gtest_brief=1` |
| `vimficiency_fuzz_tests` | FuzzTest generated-property seed corpus and fuzz campaigns | `build/tests/vimficiency_fuzz_tests` | `env FUZZTEST_FUZZ_FOR=0 FUZZTEST_PRNG_SEED=OffQXb8u5_vZtH4-7wgVOLu_HNAhPIbLz7CFF13u3nk ./build/tests/vimficiency_fuzz_tests --gtest_brief=1` (seed-only) / `./build-fuzz/tests/vimficiency_fuzz_tests --fuzz=<TestName> --fuzz_for=30s` (campaign) |
| `tests/lua/run.sh` | Lua/Neovim integration | repo root | `bash tests/lua/run.sh` |
| `vimficiency_benchmarks` | Google Benchmark suites | `build/tests/vimficiency_benchmarks` | `./build/tests/vimficiency_benchmarks` |
| `vimficiency_debug` | Scratch/debug tests | `build/tests/vimficiency_debug` | `./build/tests/vimficiency_debug` |

`VIMF_ENABLE_FUZZTEST` is on by default. Disable it only when you need a narrow
build that avoids fetching/building the FuzzTest stack:

```bash
cmake -B build -DVIMF_ENABLE_FUZZTEST=OFF
```

## Running Tests

```bash
# Build the normal tree
cmake --build build -j

# Run deterministic C++ correctness tests
./build/tests/vimficiency_tests --gtest_brief=1

# Run Lua integration tests
bash tests/lua/run.sh

# Run all default correctness checks through the helper script
bash scripts/test.sh

# Run all benchmarks
./build/tests/vimficiency_benchmarks

# Run debug scratch tests
./build/tests/vimficiency_debug
```

It is reasonable to run one test type during local iteration:

```bash
# Golden/expect fixtures
./build/tests/vimficiency_tests --gtest_filter="*Golden*Test.*"

# Oracle/conformance tests, where suite names have been migrated
./build/tests/vimficiency_tests --gtest_filter="*OracleConformanceTest.*"

# FuzzTest generated properties, deterministic seed-only mode
env FUZZTEST_FUZZ_FOR=0 \
  FUZZTEST_PRNG_SEED=OffQXb8u5_vZtH4-7wgVOLu_HNAhPIbLz7CFF13u3nk \
  ./build/tests/vimficiency_fuzz_tests --gtest_filter="*GeneratedProperty*"

# One generated-property suite with local exploration enabled
./build/tests/vimficiency_fuzz_tests \
  --gtest_filter="TransformOptimizerGeneratedPropertyTest.*"
```

The suite is only partially migrated to type-first names. For older files, check:

```bash
./build/tests/vimficiency_tests --gtest_list_tests
./build/tests/vimficiency_fuzz_tests --gtest_list_tests
```

## Fuzz Campaigns

Fuzz campaigns use the same `vimficiency_fuzz_tests` source files, but the build
must be configured in FuzzTest fuzzing mode with Clang:

```bash
cmake -S . -B build-fuzz \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DVIMF_ENABLE_FUZZTEST=ON \
  -DFUZZTEST_FUZZING_MODE=ON

cmake --build build-fuzz -j --target vimficiency_fuzz_tests

./build-fuzz/tests/vimficiency_fuzz_tests \
  --fuzz=ParserBoundaryFuzzTest.MovementParserRejectsOrReturnsViewsInsideInput \
  --fuzz_for=30s
```

Keep fuzz targets narrow and deterministic. Good targets are fallible parser,
payload, snapshot, and FFI-boundary APIs where arbitrary input should either be
accepted into a valid object or rejected cleanly. Do not fuzz APIs whose contract
is "trusted internal caller only" until a fallible boundary has been introduced.

## Writing FuzzTest Properties

Prefer direct FuzzTest domains for new non-oracle properties:

```cpp
void MergeEqualsSequentialAppend(const vector<int>& a, const vector<int>& b) {
  PhysicalKeys left = toPhysicalKeys(a);
  PhysicalKeys right = toPhysicalKeys(b);
  EXPECT_EQ(merge(left, right), append(left, right));
}

FUZZ_TEST(EffortGeneratedPropertyTest, MergeEqualsSequentialAppend)
    .WithDomains(
        fuzztest::VectorOf(fuzztest::InRange<int>(0, KEY_COUNT - 1)).WithMaxSize(16),
        fuzztest::VectorOf(fuzztest::InRange<int>(0, KEY_COUNT - 1)).WithMaxSize(16))
    .WithSeeds([]() -> std::vector<std::tuple<std::vector<int>, std::vector<int>>> {
      return {{{}, {}}, {{0}, {1, 2}}};
    });
```

Use a seed-driver parameter only when converting an existing generated loop or
when the property relies on expensive state such as `NeovimOracle`. In that
case the function should seed `RandomGen`, loop a fixed case count, and emit
`SCOPED_TRACE` with the seed and case index:

```cpp
class TransformOptimizerGeneratedPropertyTest {
 public:
  void SingleLineChangeTopResultsReplay(uint32_t seed) {
    RandomGen::seed(seed);
    for (int caseIndex = 0; caseIndex < 30; caseIndex++) {
      SCOPED_TRACE(::testing::Message() << "seed=" << seed << " case=" << caseIndex);
      // Generate a valid edit problem, optimize it, replay top results.
    }
  }
};

FUZZ_TEST_F(TransformOptimizerGeneratedPropertyTest, SingleLineChangeTopResultsReplay)
    .WithDomains(fuzztest::InRange<uint32_t>(1, 1000000))
    .WithSeeds({50});
```

The seed-driver pattern is a compatibility bridge, not the preferred shape for
new pure data-structure properties. Direct domains give FuzzTest better mutation
and shrinking behavior.

## Directory Structure

```
tests/
├── Commands/              # unit/assert: VimCore motion regressions, semantic docs, specific-output cases
├── Operator/              # unit/assert: VimCore edit/operator regressions, semantic docs, specific-output cases
├── NavOptimizer/          # unit/assert: NavOptimizer regressions, replay, cost/heuristic checks
├── TransformOptimizer/    # unit/assert: TransformOptimizer regressions, replay, cost checks
├── CompositionOptimizer/  # unit/assert: CompositionOptimizer regressions, replay
├── Optimizer/             # unit/assert: cross-optimizer parameter tests
├── Explore/               # unit/assert: explore-flow tests
├── Misc/                  # unit/assert: parser errors, cost math, config, helpers
├── Properties/            # property: FuzzTest generated-input invariants
├── Fuzz/                  # fuzz: FuzzTest coverage-guided parser/payload/boundary suites
├── Golden/                # expect/golden: curated user-facing report fixtures
├── Benchmarks/            # benchmark: Google Benchmark binary sources
├── Debug/                 # debug: scratch/debug binary sources
├── lua/                   # lua integration: Lua/Neovim plugin tests
└── Utils/                 # shared test infrastructure
```

### Placement Rule

**A test file's location is determined by its methodology.**

- Methodology-named top-level directories (`Properties/`, `Golden/`, `Fuzz/`,
  `Benchmarks/`, `Debug/`, `lua/`) hold *only* that methodology's tests. No
  exceptions.
- Feature-named top-level directories (`Commands/`, `Operator/`, the optimizer
  dirs, `Explore/`, `Misc/`) hold *only* unit/assert tests. Unit/assert is the
  implicit default — it doesn't get its own labeled directory because feature
  co-location matters more than re-labeling the default.
- A property test for a word motion goes in `tests/Properties/`, never in
  `tests/Commands/`. A unit test for a word motion goes in `tests/Commands/`,
  never in `tests/Properties/`.

`Utils/` is shared infrastructure and not subject to the rule.

## Where New Tests Belong

Decide methodology first, then pick the feature or methodology dir per the
placement rule.

- **Unit/assert** — pick the closest feature dir.
  - VimCore command/operator regressions or specific-output cases → `Commands/` or `Operator/`.
  - Optimizer regressions, replay assertions, cost/heuristic checks → matching optimizer dir.
  - Parser errors, cost math, config, data-structure invariants, helpers → `Misc/`.
  - Explore-flow tests → `Explore/`.
- **Property** — `tests/Properties/`. Generated-input invariants against the
  oracle, structural invariants, identity properties.
- **Fuzz** — `tests/Fuzz/`. Fallible parser/payload/snapshot/FFI inputs that
  must accept-or-reject cleanly.
- **Expect/golden** — `tests/Golden/`. User-facing report examples.
- **Lua integration** — `tests/lua/`.
- **Benchmark** — `tests/Benchmarks/`.
- **Debug/manual** — `tests/Debug/`, not the correctness suite.

## NeovimOracle

All VimCore behavior should match Neovim. Use `tests/Utils/NeovimOracle` to get
ground truth from an embedded `nvim --embed --headless` process. Internal C++
positions are 0-indexed; the FFI boundary handles conversion to Neovim's
1-indexed API.

The oracle is intentionally reused across calls, but it has a stability limit.
After roughly 800 buffer operations, call `restart()`. Oracle-heavy generated
properties should keep case counts small and avoid shrink patterns that can call
the oracle hundreds of extra times for one failure.

```cpp
class MyOracleConformanceTest : public ::testing::Test {
 protected:
  static unique_ptr<NeovimOracle> oracle;

  static void SetUpTestSuite() {
    oracle = make_unique<NeovimOracle>();
  }

  static void TearDownTestSuite() {
    oracle.reset();
  }
};

unique_ptr<NeovimOracle> MyOracleConformanceTest::oracle;

TEST_F(MyOracleConformanceTest, WordMotionStopsAtExpectedColumn) {
  Lines lines = {"hello world"};
  auto result = oracle->simulate(lines, 0, 0, "w");
  EXPECT_EQ(result.row, 0);
  EXPECT_EQ(result.col, 6);
}
```

For optimizer replay, prefer the shared helpers:

- `OracleReplay::matches(...)` for exact sequence replay checks.
- `OptimizerResultChecks::expectTopResultsReplay(...)` for optimizer result
  buckets that should reach a goal through Neovim.

## Seed Management

There are two seed systems:

| System | Used by | Replay mechanism |
|--------|---------|------------------|
| FuzzTest | `FUZZ_TEST` generated properties and fuzz campaigns | `.WithSeeds(...)`, `FUZZTEST_PRNG_SEED`, reproducer files |
| `SeedManager` | Benchmarks and any remaining non-FuzzTest random fixtures | `VIMFICIENCY_SEED_MODE`, `tests/.last_seeds.txt` |

For FuzzTest, the normal workflow is:

```bash
# Deterministic explicit seeds only
env FUZZTEST_FUZZ_FOR=0 \
  FUZZTEST_PRNG_SEED=OffQXb8u5_vZtH4-7wgVOLu_HNAhPIbLz7CFF13u3nk \
  ./build/tests/vimficiency_fuzz_tests

# Replay a local exploratory run
env FUZZTEST_PRNG_SEED=<printed-seed> ./build/tests/vimficiency_fuzz_tests
```

For benchmarks:

```bash
# Fixed seeds; this is also the benchmark default
./build/tests/vimficiency_benchmarks
VIMFICIENCY_SEED_MODE=fixed ./build/tests/vimficiency_benchmarks

# Replay seeds from tests/.last_seeds.txt
VIMFICIENCY_SEED_MODE=replay ./build/tests/vimficiency_benchmarks

# Rotate fixture seeds locally
VIMFICIENCY_SEED_MODE=random ./build/tests/vimficiency_benchmarks
```

## Lua Tests

The Lua layer has its own harness at `tests/lua/runner.lua`.

```bash
# Run the whole Lua suite
bash tests/lua/run.sh

# Run one Lua file
VF_TEST_FILE=tests/lua/session/store_invariants.lua \
  nvim --headless -u NONE -U NONE -l tests/lua/runner.lua
```

The main batch reuses one headless Neovim process and calls `reset_state()`
between files. Two entry-point files are deliberately isolated:

- `simulate/integration.lua`: async coroutine and tab/window state.
- `capture/on_key_mapping_probe.lua`: Neovim key-encoding characterization.

Explore flow tests are split under `tests/lua/explore/flow_*.lua`; shared
helpers belong in `tests/lua/explore/_helpers.lua`.

## Benchmarks

Benchmarks prebuild fixed fixture sets and let Google Benchmark control the
measured iteration loop. For deeper local analysis:

```bash
./build/tests/vimficiency_benchmarks \
  --benchmark_repetitions=9 \
  --benchmark_enable_random_interleaving=true \
  --benchmark_out=bench.json \
  --benchmark_out_format=json
```

Benchmark publishing is handled by the local pipeline documented in
`dev/ci-and-benchmarks.md`.

## Debugging

Use `tests/Debug/Debug.cpp` for scratch investigation. Do not add noisy print
probes to the correctness suite when a focused debug test would answer the
question faster.

For search stats/codegen checks:

```bash
cmake --build build -j --target vimficiency_debug
nm -C build/tests/vimficiency_debug | rg 'searchStatsHotLoop'
llvm-objdump -d -C build/tests/vimficiency_debug | rg -A40 'searchStatsHotLoop<false>|searchStatsHotLoop<true>'
```

## Fixture Naming

Test fixture names must not collide with production `struct` or `class` names.
C++ ODR violations from same-named test fixtures can produce silent memory
corruption.

```cpp
// Bad: may collide with a production type.
class TextObjectContext : public ::testing::Test {};

// Good: clearly a test fixture.
class TextObjectContextTest : public ::testing::Test {};
```

Check this with:

```bash
scripts/check-test-fixture-names.sh
```
