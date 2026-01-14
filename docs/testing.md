# Debugging and Testing Practices

You should have all the tools to debug any unexpected output with 100% certainty.

## Debug Printing
Use `debug()` in `Utils/Debug.h`. The project is compiled with `VIMFICIENCY_DEBUG = true` by default.

## Ground Truth: Neovim
The ground truth for the output of vim commands should be Neovim itself. Use `test/Utils/NeovimOracle` to directly get Neovim's expected output.

## Vim Documentation Reference
If you need to verify VimUtils or EditBoundary behavior or implement new commands:
- Motion commands: see `docs/vim/motion.txt`
- Change operators: see `docs/vim/change.txt`
- Command index: see `docs/vim/index.txt`

## Test Writing Guidelines
- For all non-ephemeral debugging, persist logic verification by writing a test
- Put tests in `tests/Misc` if no other places fit
- Tests should be dense, testing one aspect not covered by any other test
- Verify expected output with NeovimOracle

## Test File Naming Convention
Files in `data/TestFiles/` use this naming:
- `a1_long_line.txt`, `a2_block_lines.txt`, `a3_spaced_lines.txt` - Abstract test cases
- `m1_main_basic.txt`, `m2_main_big.txt`, `m3_source_code.txt` - Realistic code snippets

Use these for general Optimizer output testing.

## Test Utilities
The `TestUtils` class provides `TestFiles::load()` helper to read test files.
