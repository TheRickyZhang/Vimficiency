# Testing Guide

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
│   └── TestHelpers.cpp
├── MotionOptimizer/   # Optimizer output quality and correctness
│   ├── OutputCorrectnessTest.cpp
│   ├── CostConsistencyTest.cpp
│   ├── DeterminismTest.cpp
│   └── HumanApprovalTest.cpp
├── EditOptimizer/     # Same structure as MotionOptimizer
├── CompositionOptimizer/
├── Misc/              # Catch-all for other tests
├── Utils/             # Test infrastructure
│   ├── NeovimOracle.cpp
│   ├── TestUtils.cpp
│   └── EditTestGenerators.cpp
└── Debug.cpp          # Scratchpad for debugging (use DISABLED_ prefix)
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
  mt19937 rng(42);  // Fixed seed for reproducibility
  for (int i = 0; i < 100; i++) {
    auto buffer = generateRandomBuffer(rng, 5);
    Position start = randomPosition(rng, buffer);
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

- Use `tests/Debug.cpp` for scratchpad debugging with `DISABLED_` prefix
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
