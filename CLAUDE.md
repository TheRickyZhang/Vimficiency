# CLAUDE.md

## Project Overview

Vimficiency is a Vim bindings optimizer that analyzes user's actions and recommends more efficient sequences. The general algorithm is a heuristical A* search with keyboard-aware cost modeling.

**Folder structure:**
- `lua/vimficiency/`: Neovim-level code (buffer management, session storage)
- `src/Editor`: Neovim editor representation
- `src/Keyboard`: Keyboard primitives and sequence-to-key conversion
- `src/Optimizer`: Algorithm logic for optimization
- `src/State`: State tracked in Optimizer classes
- `src/VimCore`: Explicit vim motion simulation
- `src/Utils`: Utilities
- `src/lua_exports.cpp`: C++ to Lua FFI interface
- `tests/`: GoogleTest suite, mirrors src/ structure

**Dependency order** (most to least dependent): Optimizer → Editor, State → Keyboard, VimCore

## Terminology
- **Key**: Physical key (61 supported, defined in KeyboardModel.h)
- **Sequence**: String of commands in Neovim semantics
- **Motion**: Commands that only move cursor (includes jumps)
- **Edit**: Commands that change buffer (operator + motion/text object, replacement, mode change, insert typing)
- **ParsedMotion/ParsedEdit**: Command structure with count (0 = default single, positive = prefixed)
- **Effort**: Estimated difficulty of typing a key sequence
- **Position**: Contains `line`, `col`, `targetCol`. Use `pos.setCol(c)` for horizontal movements (updates both col and targetCol), `pos.clampColPreservingTarget(c)` for vertical (preserves targetCol). See `docs/vim-utils-principles.md` §5 for detailed guidance and common pitfalls.

## Design Constraints

**Cannot support** (minimal state representation):
- Screen-relative motions: gj, gk, H, M, L, zz
- Cross-buffer jumps
- Custom user mappings

**Current limitations**: No `*`, `#`, `%` motions; no search (`/`, `n`, `N`); no visual mode

## Build Commands

```bash
cmake -B build && cmake --build build
cd build && ctest
cd build && ./tests/vimficiency_tests --gtest_filter="TestName.*"
```

**Artifacts:** `build/libvimficiency_core.a`, `build/libvimficiency.so`, `build/vimficiency_cli`, `build/tests/vimficiency_tests`

**Important:**
- Don't change directories in your session! Just do everything relative to the project root.
- Never use `rm -rf build` unless something appears corrupted. The build directory contains downloaded libraries (googletest, etc.) that take time to re-fetch. For incremental rebuilds, just run `cmake --build build`.

When verifying any new feature, run all tests, but only output the last 10 lines so that we can quickly verify that everything passes. Only if there are some failures, analyze the output.

## FFI Bridge
Exposes C ABI for LuaJIT in `lua_exports.cpp`. **Position indexing:** Internal code is 0-indexed; Neovim is 1-indexed. Conversion happens at FFI boundary.

For Lua context, see `lua/CLAUDE.md`.

## Deep Dive References
- @docs/optimizer-architecture.md - A* heuristics, MotionOptimizer, EditOptimizer, CompositionOptimizer
- @docs/vim-utils-principles.md - State validation, empty handling, MovementUtils vs EndpointUtils, **targetCol handling**
- @docs/testing.md - NeovimOracle, test file conventions, debug printing
- @docs/x-macros.md - Key definitions, supported commands, sequence parsing
- @docs/boundary-logic.md - Word motion and boundary crossing logic, EditBoundary API
- @docs/edit-boundary-limitations.md - Known limitations with multi-line embedded regions
- @docs/session-invocation.txt - How vimficiency optimizer sessions are called and stored
- @docs/utils.md - General utilities (QuoteFlags, BracketFlags, Lines, StringUtils)
- @docs/edit-region-strategy.md - Replace vs change strategy (includes tryReplacement implementation)
