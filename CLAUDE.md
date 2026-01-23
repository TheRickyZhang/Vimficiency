# CLAUDE.md

## Project Overview

Vimficiency is a Vim bindings optimizer that analyzes user's actions and recommends more efficient sequences. The general algorithm is a heuristical A* search with keyboard-aware cost modeling, powered by a high-level, efficient vim command simulator.

**Folder structure:**
- `lua/vimficiency/`: Neovim-level code (buffer management, session storage)
- `src/Editor`: Neovim editor representation
- `src/Keyboard`: Keyboard primitives and sequence-to-key conversion
- `src/Optimizer`: Algorithm logic for optimization
- `src/State`: State tracked in Optimizer classes
- `src/VimCore`: Explicit vim motion simulation
- `src/Utils`: Utilities
- `src/lua_exports.cpp`: C++ to Lua FFI interface
- `tests/`: GoogleTest suite

**Dependency order** (most to least dependent): Optimizer → Editor, State → Keyboard, VimCore

## Terminology
- **Key**: Physical key (61 supported, defined in KeyboardModel.h)
- **Sequence**: String of commands in Neovim semantics
- **Motion**: Commands that only move cursor (includes jumps)
- **Edit**: Commands that change buffer (operator + motion/text object, replacement, mode change, insert typing)
- **ParsedMotion/ParsedEdit**: Command structure with count (0 = default single, positive = prefixed)
- **Effort**: Estimated difficulty of typing a key sequence
- **Position**: Contains `line`, `col`, `targetCol`. Use `pos.setCol(c)` for horizontal movements (updates both col and targetCol), `pos.clampColPreservingTarget(c)` for vertical (preserves targetCol). See `docs/vim-utils-principles.md` §5 for detailed guidance and common pitfalls.
- Effective characters: All positions a character could be in Lines. Notably, a cursor can still be at an empty line. 
- Effective lines: current region + prefix, suffix added

## Design Constraints

**Cannot support** (minimal state representation):
- Screen-relative motions: gj, gk, H, M, L, zz
- Cross-buffer jumps
- Custom user mappings

**Current limitations**: No `*`, `#`, `%` motions; no search (`/`, `n`, `N`); no visual mode

## Important Design to keep in mind!
- All positions are 0-indexed
- Always use our Lines type to represent buffer content, which provides additional helpful methods
- We allow an empty line, which has size() == 0, but still an index 0 as a valid cursor position
- But, we do not allow no lines in the buffer, since the cursor must always be in a valid position.
- Ensure CAREFUL handling of targetCol (Vim's curswant) within Position.h by calling the correct column method
- We use [begin, end) for half-open intervals, and \[start, end\] for inclusive intervals

## Build Commands

```bash
cmake --build build -j
./build/tests/vimficiency_tests --gtest_brief=1 --gtest_filter="TestName.*"
```

**Artifacts:** `build/libvimficiency_core.a`, `build/libvimficiency.so`, `build/vimficiency_cli`, `build/tests/vimficiency_tests`

**Important:**
- Don't change directories in your session! Just do everything relative to the project root.
- Never use `rm -rf build` unless something appears corrupted. The build directory contains downloaded libraries (googletest, etc.) that take time to re-fetch.

When verifying any new feature, run all tests, but only analyze if there is some failed output that is related to the current task.

## FFI Bridge
Exposes C ABI for LuaJIT in `lua_exports.cpp`. **Position indexing:** Internal code is 0-indexed; Neovim is 1-indexed. Conversion happens at FFI boundary.

For Lua context, see `lua/CLAUDE.md`.

## Deep Dive References
- @docs/boundary-logic.md - Word motion and boundary crossing logic, EditBoundary API
- @docs/edit-boundary-limitations.md - Known limitations with multi-line embedded regions
- @docs/edit-region-strategy.md - Replace vs change strategy (includes tryReplacement implementation)
- @docs/optimizer-architecture.md - A* heuristics, MotionOptimizer, EditOptimizer, CompositionOptimizer
- @docs/session-invocation.txt - How vimficiency optimizer sessions are called and stored
- @docs/testing.md - NeovimOracle, test file conventions, debug printing
- @docs/utils.md - General utilities (QuoteFlags, BracketFlags, Lines, StringUtils)
- @docs/vim-utils-principles.md - State validation, empty handling, MotionUtils vs EndpointUtils, **targetCol handling**
- @docs/x-macros.md - Key definitions, supported commands, sequence parsing
