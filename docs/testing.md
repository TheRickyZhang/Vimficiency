# Debugging and Testing Practices

You should have all the tools to debug any unexpected output with 100% certainty.

## Debug Printing
Use `debug()` in `Utils/Debug.h`. The project is compiled with `VIMFICIENCY_DEBUG = true` by default.

## Ground Truth: Neovim
The ground truth for the output of vim commands should be Neovim itself. Use `tests/Utils/NeovimOracle` to directly get Neovim's expected output.

## Vim Documentation Reference
If you need to verify VimCore or EditBoundary behavior or implement new commands:
- Motion commands: see `docs/vim/motion.txt`
- Change operators: see `docs/vim/change.txt`
- Command index: see `docs/vim/index.txt`

## NeovimOracle Usage

### Basic Usage
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
  auto result = oracle->simulate(lines, 0, 0, "w");  // row/col are 0-indexed
  EXPECT_EQ(result.row, 0);
  EXPECT_EQ(result.col, 6);  // result is also 0-indexed
}
```

### Buffer Exhaustion and restart()
The NeovimOracle maintains a persistent Neovim subprocess. After ~800 buffer
operations (create/delete cycles), the connection may become unstable due to
internal state accumulation.

**Solution**: Call `oracle->restart()` periodically between test groups:
```cpp
// After running 800 iterations of tests...
TEST_F(MyTest, NextGroup_FirstTest) {
  oracle->restart();  // Reset Neovim subprocess
  // ... test code
}
```

Rule of thumb: restart before each major test group that uses ~100+ iterations.
See `tests/Commands/BoundaryMotions.cpp` for example placement.

### Architecture
- Communicates via msgpack-RPC over stdin/stdout with `nvim --embed --headless`
- Each `simulate()` call: creates scratch buffer → sets content → runs keys → reads result → deletes buffer
- Single Neovim process reused across all tests in a suite

## Test Writing Guidelines
- For all non-ephemeral debugging, persist logic verification by writing a test
- Put tests in `tests/Misc` if no other places fit
- Tests should be dense, testing one aspect not covered by any other test
- Verify expected output with NeovimOracle

## Preferred Testing Strategy: Randomized + Oracle

For vim motion/edit behavior, prefer randomized testing over manual edge cases:

### Why Randomized Tests
- Catches edge cases you wouldn't think to test manually
- Single test covers many scenarios (typically 100 iterations)
- Self-documenting: if it passes with random input, the logic is robust

### Pattern for Motion Tests
```cpp
void testMotionRandom(NeovimOracle& oracle, const string& motion,
                      int iterations, int numLines) {
  mt19937 rng(42);  // Fixed seed for reproducibility
  for (int i = 0; i < iterations; i++) {
    auto buffer = generateRandomBuffer(rng, numLines);
    Position start = randomPosition(rng, buffer);

    // Our implementation
    Position ours = applyMotion(start, motion, buffer);

    // Neovim ground truth
    auto result = oracle.simulate(buffer, start.line, start.col, motion);
    Position expected(result.row, result.col);

    EXPECT_EQ(ours, expected) << "Failed on iteration " << i;
  }
}
```

### When to Add Manual Tests
- Specific edge case that failed and was fixed (regression test)
- Documenting expected behavior for tricky scenarios
- Cases that random generation won't hit (specific buffer structures)

## Test File Naming Convention
Files in `data/TestFiles/` use this naming:
- `a1_long_line.txt`, `a2_block_lines.txt`, `a3_spaced_lines.txt` - Abstract test cases
- `m1_main_basic.txt`, `m2_main_big.txt`, `m3_source_code.txt` - Realistic code snippets

Use these for general Optimizer output testing.

## Test Utilities
The `TestUtils` class provides `TestFiles::load()` helper to read test files.
