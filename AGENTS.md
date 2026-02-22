# CLAUDE.md

## Project Overview

Vimficiency is a Vim bindings optimizer that analyzes user's actions and recommends more efficient sequences. The general algorithm is a heuristical A* search with keyboard-aware cost modeling, powered by a high-level, efficient vim command simulator.

**Folder structure:**
- `src/VimTypes`: Shared vim-facing value types and enums (positions/ranges/modes/sequences/lines/flags/landing and edge categories)
- `lua/vimficiency/`: Neovim-level code (buffer management, session storage)
- `src/Interpreter`: Arbitrary command parsing/interpreting adapters (`parse*`, `apply*`, `simulate*`)
- `src/Session`: Snapshot/session file I/O
- `src/Keyboard`: Keyboard primitives and sequence-to-key conversion
- `src/Optimizer`: Algorithm logic for optimization
- `src/State`: State tracked in Optimizer classes
- `src/VimCore`: Explicit vim motion simulation
- `src/Utils`: Utilities
- `src/lua_exports.cpp`: C++ to Lua FFI interface
- `tests/`: GoogleTest suite

**Dependency order** (most to least dependent): Optimizer → State/Interpreter/VimCore/Keyboard/Boundary/Utils/VimTypes, State → Interpreter/VimCore/Keyboard/Utils/VimTypes, Interpreter → VimCore/Keyboard/Utils/VimTypes, Session → VimTypes, VimCore → Boundary/Utils/VimTypes, Keyboard → Utils/VimTypes, Boundary → Utils/VimTypes

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

## Important Debug Principles
Always use tests/Debug to investigate complex issues through direct, side-by-side comparison using NeovimOracle, finding the exact point our state differs from expectation.

## Specific Principles
- All positions are 0-indexed
- Always use our Lines type to represent buffer content, which provides additional helpful methods
- We allow an empty line, which has size() == 0, but still an index 0 as a valid cursor position
- But, we do not allow no lines in the buffer, since the cursor must always be in a valid position.
- Ensure CAREFUL handling of targetCol (Vim's curswant) within Position.h by calling the correct column method
- For pre/post state, we use initial, goal, such as initialLines, goalLines.

## Design Principles
- **Correctness first, then speed.** The EditOptimizer uses correct A* goal recording: goal states go through the priority queue and are recorded at pop time, guaranteeing lowest-cost results. This costs ~2x vs the old "record first-found" approach but eliminates suboptimal results from inadmissible heuristic ordering. The heuristic is inadmissible (overestimates), so pop-time recording is necessary for correctness. Accept this cost; do not regress to eager first-found recording.
- We use [begin, end) for half-open intervals, and \[first, last\] for inclusive intervals, such as beginPos/goalPos, firstPos/lastPos
- Command parsing functions are only use for arbitrarily parsing commands. For all searches, we should know the exact actions to do for minimal wasted work.
- Always use TypeScript over JavaScript for website actions. We generally prefer more modern technologies and libraries where possible.

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

**Important:**
- Never use `rm -rf build` unless something appears corrupted. The build directory contains downloaded libraries (googletest, etc.) that take time to re-fetch.
- Do not use python or write to tmp for debugging! Always debug print in tests/debug.

Generally, only run the regular (correctness) test suite after making a change to ensure compatibility. You should only run benchmarks when making a significant optimizer algorithmic change.

## FFI Bridge
Exposes C ABI for LuaJIT in `lua_exports.cpp`. **Position indexing:** Internal code is 0-indexed; Neovim is 1-indexed. Conversion happens at FFI boundary.

For Lua context, see `lua/CLAUDE.md`.

## Deep Dive References
- @docs/ci-and-benchmarks.md - CI workflow (test/benchmark/deploy), benchmark dashboard (`bench-dashboard/`), gh-pages layout
- @docs/boundary-logic.md - Word motion and boundary crossing logic, EditBoundary API
- @docs/edit-region-strategy.md - Replace vs change strategy (includes tryReplacement implementation)
- @docs/neovim_on_key_issues.md - vim.on_key limitations, operator-pending duplication, missing text object keys
- @docs/optimizer-architecture.md - A* heuristics, MotionOptimizer (6-class motion exploration, templated specs), EditOptimizer, CompositionOptimizer
- @docs/session-invocation.txt - How vimficiency optimizer sessions are called and stored
- @docs/testing.md - NeovimOracle, test file conventions, debug printing
- @docs/utils.md - General utilities (QuoteFlags, BracketFlags, Lines, StringUtils)
- @docs/vim-utils-principles.md - State validation, empty handling, MotionUtils vs EndpointUtils, **targetCol handling**
- @docs/core/keyboard.md - Keyboard module, key definitions (X macros), sequence-to-keys conversion, effort model
- @docs/core/counted-edit-semantics.md - Why `{n}{edit}` ≠ `{edit}` repeated n times, safe counted edit generation strategy
