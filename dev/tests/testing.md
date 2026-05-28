# Testing Guide

## Test Methodology Model

The primary axis is *how* a test produces evidence, not what feature it covers.
Methodology determines the runner, runtime, compiler flags, and CI policy;
features (oracle conformance, optimizer replay, parser invariants, etc.) are a
sub-layer that show up *within* a methodology — sometimes under more than one,
when the methodologies prove different things about the same feature.

| Methodology | How it proves something | Runner | Default gate | Command |
|-------------|-------------------------|--------|--------------|---------|
| Unit/assert | Hand-written deterministic check of a fixed case | `vimfy_unit_tests` | Yes | `scripts/vimfy_tests unit` |
| Approval | Curated C++ output compared to a reviewed snapshot fixture | `vimfy_approval_tests` | Yes | `scripts/vimfy_tests approval` |
| Property | Semantic invariant over generated structured project inputs | `vimfy_property_tests` | CI: fixed seed | `scripts/vimfy_tests property` |
| Safety | Safe failure or bounded behavior over adversarial inputs | `vimfy_safety_tests` | CI: fixed seed | `scripts/vimfy_tests safety` |
| Lua integration | Real-Neovim plugin/FFI scenarios | `scripts/vimfy_tests lua` | Yes | `scripts/vimfy_tests lua` |
| Benchmark | Timing and search-counter trends on fixed fixtures | `vimfy_benchmarks` | No | `./build/tests/vimfy_benchmarks` |
| Debug/manual | Scratch repros, noisy traces, human-approval exploration | `vimfy_debug` or disabled tests | No | `./build/tests/vimfy_debug` |

`scripts/vimfy_tests` is the fast local correctness gate: unit, approval,
exploratory property, exploratory safety, and Lua integration. CI calls the same
script with `seed` so generated tests are deterministic there. Property and
safety tests both use Google FuzzTest declarations and domains, but the
category boundary is the input contract and invariant, not the framework.

### What each methodology covers

Feature areas usually map to one methodology. Where a feature appears under
more than one, the methodologies are checking different things about it (e.g.
unit/assert pins regressions; property checks the universal invariant).

- **Unit/assert** — named-bug regressions, canonical-semantic documentation,
  and specific-output assertions for VimCore commands and operators
  (`tests/Unit/Commands/`, `tests/Unit/Operator/`); fixed-case optimizer replay
  regressions and small heuristic/API checks
  (`tests/Unit/NavOptimizer/`, `tests/Unit/TransformOptimizer/`,
  `tests/Unit/CompositionOptimizer/`, `tests/Unit/Optimizer/`); parser error cases, cost
  math, range and position helpers, config defaults, data-structure invariants
  (`tests/Unit/Misc/`); explore-flow tests (`tests/Unit/Explore/`). Bare oracle
  conformance (ours == oracle on hand-picked inputs) lives under `Property/`
  instead.
- **Property** — semantic invariants over structured generated project inputs:
  oracle conformance over generated motion/operator inputs, optimizer replay
  over generated edit/nav problems, structural invariants of parsed types,
  effort-model identities (`tests/Property/`).
- **Approval** — ApprovalTests.cpp snapshots for production C++ text formats
  and diagnostics (`tests/Approval/`). Lua adapters can call those formatters
  through FFI, but Neovim-owned UI behavior stays in Lua integration tests.
- **Safety** — adversarial, malformed, external-boundary, and resource-stress
  inputs whose primary contract is safe failure or bounded behavior
  (`tests/Safety/`).
- **Lua integration** — plugin lifecycle, session storage, view flows, FFI
  smoke, key-capture probes (`tests/lua/`).
- **Benchmark** — optimizer time/space/search-counter trends on fixed fixture
  seeds (`tests/Benchmarks/`).
- **Debug/manual** — scratch investigation; not gated.

Google FuzzTest is the framework for both property and safety generated tests.
It provides domains, seeds, shrinking, corpus handling, and optional
coverage-guided campaigns. Fuzzing is an execution/search mode; it is not the
directory taxonomy.

### Randomness Policy

Local property and safety runs default to FuzzTest exploration. That means
FuzzTest samples additional values from `.WithDomains(...)` and, unless
`FUZZTEST_PRNG_SEED` is already set, chooses a fresh runner seed for each run.
That runner seed controls FuzzTest's generated input stream; it is the knob for
"randomized locally, reproducible when needed."

`.WithSeeds(...)` is different: those values are explicit corpus inputs that
always run. Omit them by default; add them only for intentional smoke coverage
or pinned regressions.

CI runs the same exploration process with a fixed runner seed. The runners set:

```bash
FUZZTEST_PRNG_SEED=OffQXb8u5_vZtH4-7wgVOLu_HNAhPIbLz7CFF13u3nk
```

Leaving `FUZZTEST_FUZZ_FOR` unset keeps FuzzTest's normal unit-test exploration
loop, currently about one second per `FUZZ_TEST`. The fixed runner seed makes
that exploration deterministic in CI.

This is not an exact fixed-work contract: expensive generated tests can hit the
time cap before FuzzTest's internal iteration cap. For gate tests that need
strict work-based behavior, prefer explicit deterministic loops or checked-in
corpus seeds. Use fixed-time campaign runs for bug hunting, and measure their
throughput only when the target exposes meaningful work counters.

FuzzTest also prints `FUZZTEST_PRNG_SEED` to stderr whenever its runtime starts.
The wrapper filters that line in fixed `seed` mode on success, because the seed
is already pinned above and repeated lines are noise. If a fixed-seed run fails,
the wrapper prints the seed once with the failure output. Exploratory and
campaign runs leave FuzzTest's seed output intact so random failures, crashes,
timeouts, or interrupted runs remain reproducible.

CTest-discovered FuzzTest cases are registered with the same environment.

`scripts/vimfy_tests`, `scripts/vimfy_tests property`, and
`scripts/vimfy_tests safety` all default to exploration so local runs keep
searching for new cases. Treat those exploration runs as bug-hunting checks: a
failure should be reproduced with the printed
`FUZZTEST_PRNG_SEED`, reduced if needed in `tests/Debug`, and then pinned as a
unit regression or, when a FuzzTest corpus input is the clearest fit, a
checked-in `.WithSeeds(...)` value. Pass `seed` explicitly when you want the CI
behavior:

```bash
scripts/vimfy_tests all seed
scripts/vimfy_tests property seed
scripts/vimfy_tests property "DiffStateGeneratedPropertyTest.*" explore

env FUZZTEST_PRNG_SEED=<printed-seed> \
  scripts/vimfy_tests property "DiffStateGeneratedPropertyTest.*" explore
```

When exploratory property testing finds a real bug, add the reduced/concrete
case as a manual regression test or a checked-in `.WithSeeds(...)` corpus input
before relying on random exploration to find it again.

Recommended local cadence:

```bash
# Fast local gate, with generated tests exploring fresh values.
scripts/vimfy_tests

# CI-equivalent fixed runner seed.
scripts/vimfy_tests all seed
scripts/vimfy_tests property seed

# Randomized local exploration. Run this when you want new counterexamples.
scripts/vimfy_tests property

# Reproduce one exploratory failure.
env FUZZTEST_PRNG_SEED=<printed-seed> \
  scripts/vimfy_tests property "InterpreterMatchesOracle.MovementSequences"
```

Prefer broadening a semantic domain or running a targeted campaign over adding
manual inner random loops. Inner loops hide the generated case from FuzzTest,
which makes shrinking much less useful.

### How Property, Safety, And Fuzzing Relate

Property and safety tests use the same `FUZZ_TEST` declarations and domains.
The difference is the input domain and invariant:
- Property: structured project inputs, semantic correctness invariant.
- Safety: adversarial or malformed inputs, safe-failure or bounded-behavior
  invariant.

Fuzzing is a mode for searching either kind of property:
- Seed mode: gtest-compatible local exploration with a fixed runner seed, used
  in CI.
- Explore mode: gtest-compatible local exploration with a fresh runner seed.
- Campaign mode: coverage-guided, one selected test, sanitizer and coverage
  flags, manual runtime budget.

## Test Runners

| Binary/script | Purpose | Location | Run command |
|---------------|---------|----------|-------------|
| `vimfy_tests` | Fast local correctness gate | `scripts/vimfy_tests` | `scripts/vimfy_tests` |
| `vimfy_unit_tests` | Deterministic C++ correctness: unit, oracle, replay | `build/tests/vimfy_unit_tests` | `scripts/vimfy_tests unit` |
| `vimfy_approval_tests` | ApprovalTests.cpp C++ output snapshots | `build/tests/vimfy_approval_tests` | `scripts/vimfy_tests approval` |
| `vimfy_property_tests` | Structured semantic properties | `build/tests/vimfy_property_tests` | `scripts/vimfy_tests property` |
| `vimfy_safety_tests` | Adversarial-input safety properties | `build/tests/vimfy_safety_tests` | `scripts/vimfy_tests safety` |
| `scripts/vimfy_tests lua` | Lua/Neovim integration | repo root | `scripts/vimfy_tests lua` |
| `vimfy_benchmarks` | Google Benchmark suites | `build/tests/vimfy_benchmarks` | `./build/tests/vimfy_benchmarks` |
| `vimfy_debug` | Scratch/debug tests | `build/tests/vimfy_debug` | `./build/tests/vimfy_debug` |

FuzzTest is part of the normal test build. Property and safety tests should be
available anywhere the C++ test suite is built.

## Running Tests

```bash
# Build the normal tree
cmake --build build -j

# Run deterministic C++ correctness tests
scripts/vimfy_tests unit

# Run Lua integration tests
scripts/vimfy_tests lua

# Run all default correctness checks through the helper script
scripts/vimfy_tests

# Run all benchmarks
./build/tests/vimfy_benchmarks

# Run debug scratch tests
./build/tests/vimfy_debug
```

It is reasonable to run one test type during local iteration:

```bash
# Approval snapshots
scripts/vimfy_tests approval

# Accept approval snapshot changes, then rerun the same approval tests
scripts/vimfy_tests approval --approve
scripts/vimfy_tests approval "TreeDiffApproval.*" --approve

# Oracle/conformance tests, where suite names have been migrated
scripts/vimfy_tests unit "*OracleConformanceTest.*"

# Generated properties with CI-equivalent fixed runner seed
scripts/vimfy_tests property "*GeneratedProperty*" seed

# All generated properties with CI-equivalent fixed runner seed
scripts/vimfy_tests property seed

# One generated-property suite with local exploration, also the direct default
scripts/vimfy_tests property "TransformOptimizerGeneratedPropertyTest.*" explore
```

The suite is only partially migrated to type-first names. For older files, check:

```bash
./build/tests/vimfy_unit_tests --gtest_list_tests
./build/tests/vimfy_property_tests --gtest_list_tests
./build/tests/vimfy_safety_tests --gtest_list_tests
```

### Editor Helper

Neovim-local test running support lives in
`dev/editor/vimfy-test-helper.lua`. It maps the current file to the narrow
CMake target, then runs through `scripts/vimfy_tests` so seed and runner policy
stay centralized. It recognizes the common `TEST*`, `TYPED_TEST*`, and
`FUZZ_TEST*` macros.

Load it from personal config and bind it there:

```lua
local vimfy_tests = dofile("/path/to/vimficiency/dev/editor/vimfy-test-helper.lua")
vim.keymap.set("n", "<leader>tt", vimfy_tests.run_gtest_here)
```

## Fuzz Campaigns

Fuzz campaigns use the same property or safety source files, but the build must
be configured in FuzzTest fuzzing mode with Clang:

```bash
cmake -S . -B build-fuzz \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DFUZZTEST_FUZZING_MODE=ON

cmake --build build-fuzz -j --target vimfy_safety_tests

VIMFY_BUILD_DIR=build-fuzz \
  scripts/vimfy_tests safety \
  ParserBoundarySafetyTest.MovementParserRejectsInvalidInputOrReturnsBorrowedTokens \
  campaign
```

Keep fuzz targets narrow and deterministic. Good targets are fallible parser,
payload, snapshot, and FFI-boundary APIs where arbitrary input should either be
accepted into a valid object or rejected cleanly. Do not fuzz APIs whose contract
is "trusted internal caller only" until a fallible boundary has been introduced.

## Writing FuzzTest Properties

Prefer direct FuzzTest domains for new or migrated properties:

```cpp
void RoundTripAndStructureAcrossBufferStyles(
    const PropertyDomains::DiffCaseSpec& spec) {
  Lines initial(spec.initial);
  Lines goal = spec.identity ? initial : Lines(spec.goal);
  EXPECT_EQ(Myers::applyAllDiffState(Myers::calculate(initial, goal), initial),
            goal);
}

FUZZ_TEST(DiffStateGeneratedPropertyTest, RoundTripAndStructureAcrossBufferStyles)
    .WithDomains(PropertyDomains::DiffCaseSpecDomain());
```

For optimizer replay and oracle-conformance properties, prefer a shrinkable
case-spec struct over a seed-driver loop. Put the semantic pieces FuzzTest
should shrink directly into the domain: lines, cursor indices, command tokens,
edit/mutation descriptors, boundary shape, and goal text. The test can still
build richer project objects from that spec, but failure output should print
the converted semantic case.

Direct semantic domains give FuzzTest better mutation and shrinking behavior.
In either style, omit `.WithSeeds(...)` by default. Add it only for intentional
checked-in corpus inputs, such as reduced regressions or rare smoke cases that
the generator might not hit quickly.

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
├── Property/              # property: structured semantic generated-input invariants
├── Safety/                # safety: adversarial-input safe-failure and bounded-behavior checks
├── Approval/              # approval: ApprovalTests.cpp C++ output snapshots
├── Benchmarks/            # benchmark: Google Benchmark binary sources
├── Debug/                 # debug: scratch/debug binary sources
├── lua/                   # lua integration: Lua/Neovim plugin tests
└── Utils/                 # shared test infrastructure
```

We keep tests centralized under `tests/` because build wiring, shared
NeovimOracle infrastructure, and methodology-specific runners matter more here
than colocating tests next to implementation files.

Naming uses singular methodology directories: `Property/`, `Safety/`,
`Approval/`. Files in those directories use `SubjectRole.cpp`, such as
`NavOptimizerProperty.cpp` or `ParserBoundarySafety.cpp`, so editor tabs and
build output do not collide with production basenames.

### Placement Rule

**A test file's location is determined by its methodology.**

- Methodology-named top-level directories (`Property/`, `Safety/`, `Approval/`,
  `Benchmarks/`, `Debug/`, `lua/`) hold *only* that methodology's tests. No
  exceptions.
- Feature-named top-level directories (`Commands/`, `Operator/`, the optimizer
  dirs, `Explore/`, `Misc/`) hold *only* unit/assert tests. Unit/assert is the
  implicit default — it doesn't get its own labeled directory because feature
  co-location matters more than re-labeling the default.
- A property test for a word motion goes in `tests/Property/`, never in
  `tests/Unit/Commands/`. A unit test for a word motion goes in `tests/Unit/Commands/`,
  never in `tests/Property/`.

`Utils/` is shared infrastructure and not subject to the rule.

## Where New Tests Belong

Decide methodology first, then pick the feature or methodology dir per the
placement rule.

- **Unit/assert** — pick the closest feature dir.
  - VimCore command/operator regressions or specific-output cases → `Commands/` or `Operator/`.
  - Optimizer regressions, replay assertions, cost/heuristic checks → matching optimizer dir.
  - Parser errors, cost math, config, data-structure invariants, helpers → `Misc/`.
  - Explore-flow tests → `Explore/`.
- **Property** — `tests/Property/`. Structured generated-input invariants
  against the oracle, optimizer replay, structural invariants, identity
  properties.
- **Safety** — `tests/Safety/`. Adversarial, malformed, external-boundary, and
  resource-stress inputs that must fail safely or remain bounded.
- **Approval** — `tests/Approval/`. ApprovalTests.cpp snapshots for production
  C++ text formats and diagnostics. Lua adapters to those formatters stay
  covered by Lua integration tests.
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
| FuzzTest | `FUZZ_TEST` generated properties and fuzz campaigns | `.WithSeeds(...)` corpus inputs, `FUZZTEST_PRNG_SEED`, reproducer files |
| `SeedManager` | Benchmarks and any remaining non-FuzzTest random fixtures | `VIMFY_SEED_MODE`, `tests/.last_seeds.txt` |

For FuzzTest, the normal workflow is:

```bash
# Deterministic replay of the normal exploration loop
env FUZZTEST_PRNG_SEED=OffQXb8u5_vZtH4-7wgVOLu_HNAhPIbLz7CFF13u3nk \
  ./build/tests/vimfy_property_tests

# Replay a local exploratory run
env FUZZTEST_PRNG_SEED=<printed-seed> ./build/tests/vimfy_property_tests
```

For benchmarks:

```bash
# Fixed seeds; this is also the benchmark default
./build/tests/vimfy_benchmarks
VIMFY_SEED_MODE=fixed ./build/tests/vimfy_benchmarks

# Replay seeds from tests/.last_seeds.txt
VIMFY_SEED_MODE=replay ./build/tests/vimfy_benchmarks

# Rotate fixture seeds locally
VIMFY_SEED_MODE=random ./build/tests/vimfy_benchmarks
```

## Lua Tests

The Lua layer has its own harness at `tests/lua/runner.lua`.

```bash
# Run the whole Lua suite
scripts/vimfy_tests lua

# Run one Lua file
scripts/vimfy_tests lua tests/lua/session/store_invariants.lua
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
./build/tests/vimfy_benchmarks \
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
cmake --build build -j --target vimfy_debug
nm -C build/tests/vimfy_debug | rg 'searchStatsHotLoop'
llvm-objdump -d -C build/tests/vimfy_debug | rg -A40 'searchStatsHotLoop<false>|searchStatsHotLoop<true>'
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
