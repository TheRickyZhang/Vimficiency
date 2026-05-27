# AGENTS.md

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
- `src/lua_exports.cpp`, `src/LuaExports/`: C++ to Lua FFI interface
- `tests/`: GoogleTest suite

## Terminology (Brief)
General:
- **Key**: Physical key
- **Token**: Atomic parsed unit of a Vim sequence
- **Sequence**: Neovim command string
- **Effort**: Estimated typing difficulty of a key sequence only, independent of search distance
- **Step**: Small replay/execution unit, usually one parsed token or one immediate user-action token
- **Phase**: Larger Explore state-machine unit: `Navigate`, `Transform`, `Insert`, or `Completed`
- **Planned Edit**: One composition-plan edit boundary, from fencepost `i` to `i + 1`; use `plannedEdit`/`plannedEditAt`, not "composition step"

- **Movement**: Commands that change cursor position without changing text
- **Edit**: Commands that change buffer contents, mode, or both, such as operator + motion/text object, replacement, mode change, insert typing
- **Nav**: Movement-oriented actions/results whose to navigate without changing text
- **Transform**: Modify-oriented actions/results to change text
- Note that these intentionally distinct from Vim's narrower categories, such as motion, to avoid name collision.
- Note NavOptimizer may use movements in its search, but it also uses jumps and find commands, hence the distinction. Similarly, TransformOptimizer may primarily search edits, but it may also search substitute commands as well.

- **Begin/End**: Half-open range, `[begin, end)`
- **First/Last**: Inclusive range, `[first, last]`

C++ (Internal representation):
- **KeyedSequence**: Sequence + physical keys used to type it
- **Sequence Binding**: KeyedSequence + precomputed RunningEffort
- **ParsedMovement/ParsedEdit**: Command structure with count semantics, where count `0` means the default implicit count of 1

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
- Do not use python or write to tmp for debugging! Always debug print in tests/Debug.
- Generally, only run the corresponding test suite after making a logic change to ensure compatibility. Lua for lua, C++ for C++.

## Design Constraints

**Cannot support** (minimal state representation):
- Screen-relative motions: gj, gk, H, M, L, zz
- Cross-buffer jumps
- Custom user mappings

## Important Principles
- Always use tests/Debug to investigate complex issues through direct, side-by-side comparison using NeovimOracle, finding the exact point our state differs from expectation.
- Don't use static casts if they aren't needed!
- Don't overuse namespaces. We should naturally bring them up as the code grows, not when we only have a few functions / classes.
- Make sure to route unicode printing through PrettyPrint
- Before introducing new variables, consider if there are similar existing ones!
- When fixing an edge case in vim semantics, you should be wary of creating a net increase in logic. It is very easy to provide bloated hotfixes instead of precisely outlining the issue.

## Invariants
- All positions in C++ are 0-indexed
- We use [begin, end) for half-open intervals, and \[first, last\] for inclusive intervals, such as beginPos/goalPos, firstPos/lastPos
- Motion targets are inclusive (`CharInterval`), while edit/diff ranges remain half-open (`CharRange`). Convert at boundaries only; do not mix semantics inside NavOptimizer internals.
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
| `vimfy_unit_tests` | Unit tests | `./build/tests/vimfy_unit_tests --gtest_brief=1` |
| `vimfy_approval_tests` | Approval snapshot tests | `./build/tests/vimfy_approval_tests --gtest_brief=1` |
| `vimfy_property_tests` | Structured property tests | `scripts/vimfy_tests property` |
| `vimfy_safety_tests` | Safety tests | `scripts/vimfy_tests safety` |
| `vimfy_benchmarks` | Performance benchmarks | `./build/tests/vimfy_benchmarks` |
| `vimfy_debug` | Scratch/debug tests | `./build/tests/vimfy_debug` |

```bash
# Run all correctness tests
scripts/vimfy_tests

# Run all benchmarks
./build/tests/vimfy_benchmarks
```

**Other artifacts:** `build/libvimfy_core.a`, `build/libvimficiency.so`, `build/vimficiency_cli`


## FFI Bridge
Exposes C ABI for LuaJIT through `src/lua_exports.cpp` and the domain export files under `src/LuaExports/`. **Position indexing:** Internal code is 0-indexed; Neovim is 1-indexed. Conversion happens at FFI boundary.

For Lua context, see `lua/vimficiency/AGENTS.md`.

## Deep Dive References
- @dev/ci-and-benchmarks.md - CI workflow (test/benchmark/deploy), benchmark dashboard (`bench-dashboard/`), gh-pages layout
- @dev/core/boundary-logic.md - Word motion and boundary crossing logic, TransformBoundary API
- @dev/architecture/module-dependencies.md - Allowed C++ module dependency graph and placement rules
- @dev/core/utils.md - General utilities (QuoteFlags, BracketFlags, Lines, StringUtils)
- @dev/core/vim-edge-cases.md - Delete/change edge cases, autoindent, and VimCore behavior traps
- @dev/core/keyboard.md - Keyboard module, key definitions (X macros), sequence-to-keys conversion, effort model
- @dev/core/counted-edit-semantics.md - Why `{n}{edit}` != `{edit}` repeated n times, safe counted edit generation strategy
- @dev/lua/neovim_on_key_issues.md - vim.on_key limitations, operator-pending duplication, missing text object keys
- @dev/lua/session-invocation.md - How vimficiency optimizer sessions are called and stored
- @dev/optimizer/optimizer-architecture.md - A* heuristics, NavOptimizer, TransformOptimizer, CompositionOptimizer
- @dev/optimizer/transform-optimizer.md - Transform search, replacement/change strategy, and goal suffix handling
- @dev/optimizer/composition-optimizer.md - Planned-edit search and composition replay semantics
- @dev/optimizer/interactive-explore.md - Explore state machine and interactive replay model
- @dev/tests/testing.md - NeovimOracle, test file conventions, debug printing

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
