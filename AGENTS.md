# CLAUDE.md

## Project Overview

Vimficiency is a Vim bindings optimizer that analyzes a user's actions and recommends more efficient sequences. The general algorithm is a heuristical A* search with keyboard-aware cost modeling, powered by a high-level, efficient vim command simulator.

**Folder structure:**
- `src/Types`: Shared vim-facing value types and enums
- `lua/vimficiency/`: Neovim-level code (buffer management, session storage)
- `src/Interpreter`: Arbitrary command parsing/interpreting
- `src/Session`: Snapshot/session file I/O
- `src/Keyboard`: Keyboard primitives and sequence-to-key conversion
- `src/Effort`: Effort accumulation/cache built on keyboard primitives
- `src/Optimizer`: Algorithm logic for optimization
- `src/VimCore`: Explicit vim motion simulation
- `src/Utils`: Utilities
- `src/lua_exports.cpp`: C++ to Lua FFI interface
- `tests/`: GoogleTest suite

## Terminology (Brief)
General:
- **Key**: Physical key
- **Token**: Atomic parsed unit of a Vim sequence
- **Sequence**: Neovim command string
- **Effort**: Estimated typing difficulty of a key sequence only, independent of search distance

- **Motion**: Commands that change cursor position without changing text
- **Nav**: Movement-oriented actions/results whose practical outcome is navigation without changing text
- **Edit**: Commands that change buffer contents, mode, or both, such as operator + motion/text object, replacement, mode change, insert typing
- Note these are distinct from Vim's narrower grammatical categories, focusing on practical outcomes during optimization

- **Begin/End**: Half-open range, `[begin, end)`
- **First/Last**: Inclusive range, `[first, last]`

C++ (Internal representation):
- **KeyedSequence**: Sequence + physical keys used to type it
- **Sequence Binding**: KeyedSequence + precomputed RunningEffort
- **ParsedMotion/ParsedEdit**: Command structure with count semantics, where count `0` means the default implicit count of 1

- **Pos**: Only contains `line` and `col`
- **CursorPos**: Adds `targetCol`; use `setCol(c)` vs `clampColPreservingTarget` when Vim's richer curswant is needed
- **Line/Lines**: Richer buffer-text containers with helpers like `effectiveSize()`, `flatten()`, and `unflatten()`
- **Mode**: Vim editing mode tracked by simulation/search state

- **Goal**: Exact desired post-action state, especially `goalPos`
- **Boundary**: Allowed traversal region during search
- **Local/Global coordinates**: Relative to the current slice vs the larger source buffer
- **Heuristic**: Estimated remaining search work = distance (closeness to target) + cost - penalty

Lua (User concepts):
- **Session**: Captured editing instance and stored optimization result
- **View**: Anything that lets you interactively engage with a finished session. Currently includes play and explore.
- **Scoped Settings**: Settings that only apply for a certain view

**Important:**
- Never use `rm -rf build` unless something appears corrupted. The build directory contains downloaded libraries (googletest, etc.) that take time to re-fetch.
- Do not use python or write to tmp for debugging! Always debug print in tests/debug.
- Generally, only run the corresponding test suite after making a logic change to ensure compatibility. Lua for lua, C++ for C++.

## Design Constraints

**Cannot support** (minimal state representation):
- Screen-relative motions: gj, gk, H, M, L, zz
- Cross-buffer jumps
- Custom user mappings

## Important Debug Principles
Always use tests/Debug to investigate complex issues through direct, side-by-side comparison using NeovimOracle, finding the exact point our state differs from expectation.

## Invariants
- All positions in C++ are 0-indexed
- We use [begin, end) for half-open intervals, and \[first, last\] for inclusive intervals, such as beginPos/goalPos, firstPos/lastPos
- Motion targets are inclusive (`CharInterval`), while edit/diff ranges remain half-open (`CharRange`). Convert at boundaries only; do not mix semantics inside MotionOptimizer internals.
- Always use our Lines type to represent buffer content, which provides additional helpful methods
- Lines and Line can be empty, but have the cursor be at index 0 (Matches Vim handling)
- Ensure CAREFUL handling of targetCol (Vim's curswant) within Position.h by calling the correct column method
- The heuristic is inadmissible (overestimates), so pop-time recording is necessary for correctness.
- Always use TypeScript over JavaScript, Bun over NPM for website actions. We generally prefer more modern technologies and libraries where possible.

## Build Commands

```bash
cmake --build build -j
```

**Test binaries** (in `build/tests/`):
| Binary | Purpose | Example |
|--------|---------|---------|
| `vimficiency_tests` | Unit tests | `./build/tests/vimficiency_tests --gtest_brief=1` |
| `vimficiency_benchmarks` | Performance benchmarks | `./build/tests/vimficiency_benchmarks` |
| `vimficiency_debug` | Scratch/debug tests | `./build/tests/vimficiency_debug` |

```bash
# Run all correctness tests
./build/tests/vimficiency_tests --gtest_brief=1

# Run all benchmarks
./build/tests/vimficiency_benchmarks
```

**Other artifacts:** `build/libvimficiency_core.a`, `build/libvimficiency.so`, `build/vimficiency_cli`


## FFI Bridge
Exposes C ABI for LuaJIT in `lua_exports.cpp`. **Position indexing:** Internal code is 0-indexed; Neovim is 1-indexed. Conversion happens at FFI boundary.

For Lua context, see `lua/CLAUDE.md`.

## Deep Dive References
- @dev/ci-and-benchmarks.md - CI workflow (test/benchmark/deploy), benchmark dashboard (`bench-dashboard/`), gh-pages layout
- @dev/boundary-logic.md - Word motion and boundary crossing logic, EditBoundary API
- @dev/edit-region-strategy.md - Replace vs change strategy (includes tryReplacement implementation)
- @dev/neovim_on_key_issues.md - vim.on_key limitations, operator-pending duplication, missing text object keys
- @dev/optimizer-architecture.md - A* heuristics, MotionOptimizer (6-class motion exploration, templated specs), EditOptimizer, CompositionOptimizer
- @dev/session-invocation.txt - How vimficiency optimizer sessions are called and stored
- @dev/testing.md - NeovimOracle, test file conventions, debug printing
- @dev/utils.md - General utilities (QuoteFlags, BracketFlags, Lines, StringUtils)
- @dev/vim-utils-principles.md - State validation, empty handling, MotionUtils vs EndpointUtils, **targetCol handling**
- @dev/core/keyboard.md - Keyboard module, key definitions (X macros), sequence-to-keys conversion, effort model
- @dev/core/counted-edit-semantics.md - Why `{n}{edit}` ≠ `{edit}` repeated n times, safe counted edit generation strategy

## PR review focus
Prioritize:
1) Logical/correctness bugs, undefined behavior, lifetime issues
2) Design/code smells and coupling that will hurt future changes
3) Long-term semantic concerns (API boundaries, invariants, hidden assumptions)

Guidelines:
- Be concise: prefer bullets.

Output expectations:
- Summarize first, then list issues.
- Inline comments only for actionable issues.
