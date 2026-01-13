# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Vimficiency is a Vim bindings optimizer that analyzes user's actions and recommends more efficient sequences. The general algorithm is a heuristical A* search with keyboard-aware cost modeling.

The folder structure is as follows:\
- lua/vimficiency*: lua files that work on the Neovim level, such as buffer management and session storage.
- src/Editor: classes for representing a Neovim editor.
- src/Keyboard: Keyboard primitives and the conversion layer from a sequence to keys.
- src/Optimizer: Algorithm logic for finding optimized results and related helpers.
- src/State: Represents the state tracked in the Optimizer classes.
- src/Utils: Utilities
- src/VimCore: Explicit simulation of vim motions using similar structures and logic.
- src/lua_exports.cpp: Interface to expose src/ to lua.
- src/main.cpp: Alternative CLI interface
- tests: Uses GoogleTest. We want to mirror for unit tests when possible, then separate by functionality type.

In general, our dependency order from most dependent to least, is:
Optimizer -> Editor, State -> Keyboard, VimCore 

## Terminology
Key: A physical key. We currently support 61 keys, which are defined in KeyboardModel.h, generated from X Macros in XMacroKeyDefinitions.h
Sequence: A string representing a sequence of commands in Neovim semantics.
Motion: Commands that only move the cursor position. Includes pure motions and jumps.
Edit: Commands that change the buffer. Includes operator + motion/text object, replacement, mode change, and typing in insert mode.
Config: contains all settings relating to keys.
Effort: The estimated difficulty of typing out a list of keys. For development, we use Config::uniform(), which assigns a weight of 1.0 to every key.
EditBoundary: structure that conveys how the boundary looked lyke.
`Position`: contains `line`, `col`, and `targetCol`. Ensure you understand targetCol's purpore in vim, and choose carefully between:
- `pos.setCol(c)` (Updates both `col` and `targetCol`, for horizontal movements)
- `pos.col = c` (Updates only `col`, preserves `targetCol` for vertical movements)

Note that Motion and Edit are defined a bit differently than Neovim's documentation.
We also have ParsedMotion and ParsedEdit, which represent an overall command structure and thus include a count. We use 0 for default single count, and positive numbers for prefixed counts.

Example parsing:
Sequence "l3wfD;" -> {ParsedMotion("l", 0), ParsedMotion("w", 3), ParsedMotion("fD;", 1)}
-> {Key(l), Key(3), Key(w), Key(f), Key(Shift), Key(d), Key(Semicolon)}.

## Data Values
The codebase uses **X macros** in `XMacroKeyDefinitions.h` to define keys/hands/fingers:

```cpp
#define VIMFICIENCY_KEYS(X) \
  X(Key_A, "a") \
  X(Key_B, "b") \
  // ...
```

This single definition generates:
- Enum values: `enum class Key { Key_A, Key_B, ... }`
- Name arrays: `const char* g_key_names[] = {"a", "b", ...}`
- FFI exports: `vimficiency_key_name(int index)`

You can find supported commands in src/Keyboard/{CharToKey, MotionToKeys, EditToKeys}.
Note we group commands by role in searching. For instance, f-commands are separate in motions, and edits are grouped based on EditBoundary level.

## Design Philosophy and Constraints
- We want our optimizers to be fast, as they should be eventually used in real time. 
- Always, always ensure that our vim implementation is fully correct, before anything more complex. Then, we can find ways to optimize them.
- Always think of edge cases.

**Cannot support** (due to minimal state representation):
- Screen-relative motions: gj, gk, H, M, L, zz
- Cross-buffer jumps
- Custom user mappings (currently only default Vim bindings)

**Current limitations**:
- No *, #, % symbol motions
- No search (/, n, N)
- No visual mode

## VimUtils Design Principles

1. **Validate state strictly** - When calling our underlying VimUtils, we should be using assertions to error on any redundant actions. This is because early pruning is always preferred, so it should be assumed when searching that we will never explore options that are easily checkable (for instance don't search j if we are on the last line). This extends to count validation of deterministic outcomes.
For instance, "j" should never be explored if on the last line, and neither should 3dd on the second to last line.

2. **Handle Empty Representation** Since we must distinguish one empty line/column, and no columns, we thus must handle emptiness explicitly.
To put it more explicitly:
`line < lines.size()` EXCEPT when lines.empty(), in which case line=0, col=0.
`col < lines[line].size()` EXCEPT when `lines[line].empty()`, in which case col=0. 

3. **Minimal API** - Single-line operations only need the context of the line.


## Optimizer Architecture and Logic
We have three different Optimizers.
They all have similar configuration settings and include E = effort for typing the key sequence in their heuristics.
When optimizing commands, we only want to send the relevant buffer context to guard against redundant searching. Thus, Optimizers take in boundary info so that gg/G, for instance, are not searched if they would "spill over".

MotionOptimizer:
- Finds best ways to move cursor from start to end.
- Does a pure A* search over possible motions
- Uses the heuristic of E + (Manhattan distance to goal)
- Builds an index over text objects for efficiency, as we guarantee the buffer contents stay the same

EditOptimizer:
- Finds best ways to change starting text to ending text, assuming that all ending text will be typed. We can start from any position, but always end at the very last character.
- It is very important to handle edit boundaries here, as start/end can span multiple lines, and straddle lines/words from the original context.
- TBD: Does a multi-source A* search over deleting all characters. Look for improvement.
- Uses the heuristic of E + (Remaining characters to delete)

CompositionOptimizer:
- Finds best ways to change any buffer state to any other buffer state by content.
- First, uses Myer's diff logic over characters to represent the change into many Diff states.
- Then, solves each Diff state using EditOptimizer. This gives us possible "actions" with a start, end, and cost, and that resolve a Diff State.
- Does a pure A* search, using MotionOptimizer to get to the next edit region, and EditResults to resolve the current edit region.
- Uses the heuristic of E + (Distance to next edit region) + (expected cost of edit regions not yet completed). We penalize overshooting distance more, to enforce resolving the edit regions in order.

For edit distance analysis, buffers are flattened to single strings with `\n` characters:
```cpp
flattenLines({"aaa", "bbb", "ccc"}) → "aaa\nbbb\nccc"
```


## Debugging and Testing Practices

You should have all the tools to debug any unexpected output with 100% certainty.

For debug printing, you may use the debug() in Utils/Debug.h. The project is compiled with VIMFICIENCY_DEBUG = true by default.

The ground truth for the output of vim commands should be Neovim itself. You can use test/Utils/NeovimOracle to directly get Neovim's expected output.

If you need to verify VimUtils or EditBoundary behavior or implement new commands, refer to `docs/reference`:
- Motion commands: see `docs/reference/motion.txt`
- Change operators: see `docs/reference/change.txt`
- Command index: see `docs/reference/index.txt`

For all non-ephemeral debugging, persist logic verification by writing a test. You may put it in `tests/Misc` if no other places fit.

Ensure that tests are dense, testing one aspect not covered by any other test, and verified with NeovimOracle.

Test files in `data/TestFiles/` use naming convention:
- `a1_long_line.txt`, `a2_block_lines.txt`, `a3_spaced_lines.txt` - Abstract test cases
- `m1_main_basic.txt`, `m2_main_big.txt`, `m3_source_code.txt` - Realistic code snippets
Use these for general Optimizer output testing.

The `TestUtils` class provides `TestFiles::load()` helper to read test files.


## Build and Test Commands

Do not run tests unless explicitly requested, or your code naturally introduces new tests.

```bash
cmake -B build && cmake --build build

cd build && ctest

# Run specific test
cd build && ./tests/vimficiency_tests --gtest_filter="OptimizerTest.*"

# Clean rebuild
rm -rf build && cmake -B build && cmake --build build
```

Build Artifacts:
- `build/libvimficiency_core.a` - Static library (core logic)
- `build/libvimficiency.so` - Shared library (Lua FFI interface)
- `build/vimficiency_cli` - Standalone CLI
- `build/tests/vimficiency_tests` - GoogleTest suite



# Lua

### FFI Bridge (lua_exports.cpp)

Exposes C ABI for LuaJIT (C++ ABI not supported by LuaJIT FFI):

```c
const char* vimficiency_analyze(const char* start_text, int start_row, int start_col,
                                const char* end_text, int end_row, int end_col,
                                const char* keyseq);
VimficiencyConfigFFI* vimficiency_get_config();
void vimficiency_apply_config();
```

**Position indexing:** Internal code uses 0-indexed rows/cols. Neovim uses 1-indexed rows, so conversion happens at FFI boundary.

For the most part, you will be working in the C++ source. If you need more Lua context, see lua/CLAUDE.md
