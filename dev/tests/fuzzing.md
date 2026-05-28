# Coverage-guided fuzzing (manual campaigns)

The default test build runs the `FUZZ_TEST`s in FuzzTest **unit mode**: blind
random generation from the domains (seeded PRNG, no coverage feedback), capped
at `min(10000 iterations, FUZZTEST_FUZZ_FOR)` per test. That's the fast
per-commit correctness gate.

**Coverage-guided campaign mode** (`--fuzz`) is the deeper variant: it
instruments the code under test, mutates inputs toward new coverage, and runs
one target continuously with no 10000 cap. It's for manual soaks / bug hunts,
not CI.

## Prerequisites

- **Clang.** FuzzTest's engine relies on Clang's sancov ABI
  (`-fsanitize-coverage=inline-8bit-counters,trace-cmp` + ASan). GCC will not
  produce a working `--fuzz` binary. (The watchdog patch in
  `tests/patches/fuzztest-watchdog.patch` still applies here — campaign teardown
  is just as prompt.)

## Build (separate dir, leaves the fast gate alone)

```bash
cmake -B build_fuzz -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DFUZZTEST_FUZZING_MODE=on
cmake --build build_fuzz --target vimfy_property_tests
```

`-DFUZZTEST_FUZZING_MODE=on` is wired in the top-level `CMakeLists.txt`: it adds
ASan + sancov to `CMAKE_CXX_FLAGS` at root scope so `vimfy_core` (the code under
test) is instrumented, not just the test binary. In this mode the unit-mode
gtest registration is skipped (`tests/CMakeLists.txt`), so these binaries run
*only* via `--fuzz`. `build_fuzz/` is gitignored (`build*/`). First build is
slow: it re-fetches and rebuilds all deps under `build_fuzz/_deps` with ASan
(no sharing with the GCC `build/`).

## Run a campaign

```bash
# List the fuzz targets (gtest discovery is off in fuzzing mode):
./build_fuzz/tests/vimfy_property_tests --list_fuzz_tests

# Time-bounded campaign on one target:
./build_fuzz/tests/vimfy_property_tests --fuzz=VerifyCharOperators.CharOperatorsMatchOracle --fuzz_for=10m

# Or via the script (reads VIMFY_BUILD_DIR; campaign mode passes --fuzz/--fuzz_for):
VIMFY_BUILD_DIR=build_fuzz VIMFY_FUZZ_FOR=10m \
  scripts/vimfy_tests property VerifyCharOperators.CharOperatorsMatchOracle campaign

# Count-bounded instead of time-bounded:
FUZZTEST_MAX_FUZZING_RUNS=200000 ./build_fuzz/tests/vimfy_property_tests --fuzz=<Suite.Test>
```

## Corpus, reproducers, ASan

- Coverage corpus defaults to `~/.cache/fuzztest`; pass `--corpus_database=<dir>`
  for a project-local corpus that accumulates coverage across runs.
- On a finding FuzzTest writes a reproducer and prints its path
  (`FUZZTEST_REPRODUCERS_OUT_DIR` controls where); replay it to debug.
- The whole binary runs under ASan, including the NeovimOracle msgpack/IPC path
  (nvim itself is a subprocess, uninstrumented). Triage early runs with
  `ASAN_OPTIONS=detect_leaks=0` if leak reports drown out real findings, then
  fix the real ones.

## Keeping the flags correct

The root-scope flag list mirrors fuzztest's `cmake/FuzzTestFlagSetup.cmake`
(`build/_deps/fuzztest-src/...`). If a FuzzTest version bump changes those
flags, update the `FUZZTEST_FUZZING_MODE` block in the top-level `CMakeLists.txt`
to match.
