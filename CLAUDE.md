# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Vimficiency is a Vim motion optimizer that analyzes cursor movements and recommends more efficient key sequences. The system uses A* search with keyboard-aware cost modeling to find optimal Vim motions between editor positions.

**Architecture:** C++ core library + LuaJIT FFI bridge + Neovim plugin

## Debugging Practices

When creating temporary test files to inspect execution behavior, use `tests/Temp.cpp` instead of `/tmp`. This keeps debug code in the project where it can be easily found and cleaned up.

## Build and Test Commands

```bash
# Build
cmake -B build && cmake --build build

# Run all tests
cd build && ctest

# Run tests with output
cd build && ctest --verbose

# Run specific test
cd build && ./tests/vimficiency_tests --gtest_filter="OptimizerTest.*"

# CLI usage (manual testing)
./build/vimficiency_cli <start_snapshot_path> <end_snapshot_path> <user_sequence>

# Clean rebuild
rm -rf build && cmake -B build && cmake --build build
```

## Core Architecture

### Search Algorithm (Optimizer/)

The optimizer performs **A* search** over editor states to find optimal motion sequences:

- **State** = Position + Mode + RunningEffort + accumulated cost
- **Search space**: Each state expands by applying available Vim motions
- **Heuristic**: `cost_weight * effort + manhattan_distance_to_goal`
- **RunningEffort** tracks typing patterns across sequences (same-finger penalties, alternation bonuses, rolls)
- **BufferIndex** pre-computes word/WORD/paragraph/sentence landing positions for efficient count motion search (e.g., finding optimal `5w` vs `wwwww`)

Key constraint: Search uses **minimal state** (contiguous buffer lines + position only), which means screen-relative motions (H, M, L, zz, gj, gk) and cross-buffer jumps are not supported.

### Motion Processing Pipeline

1. **MotionToKeys** (Keyboard/): Maps Vim motion strings (e.g., "w", "3j", "gg") to PhysicalKeys
2. **PhysicalKeys**: Vector of Key enum values representing physical keypresses (used for effort calculation)
3. **Sequence**: Struct with `{string keys, Mode mode}` for storing command sequences in state classes (used for output)
4. **Config** (Optimizer/): Defines keyboard layout (QWERTY/Colemak-DH/Uniform) with per-key costs and hand/finger assignments
5. **Motion application** (Editor/Motion.h): Applies motion semantics to Position, returns new Position + Mode

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

### Key Components

**Editor/**: Position (with targetCol), Snapshot (buffer + cursor state), Motion (movement parsing/application), Edit (edit operation dispatch), Mode enum, NavContext (window height/scroll amount), Range (for operator+motion)

**State/**: State struct (Position + RunningEffort + cost + sequence), RunningEffort (tracks typing effort patterns)

**Keyboard/**:
- KeyboardModel.h: Key/Hand/Finger enums, KeyInfo struct
- XMacroKeyDefinitions.h: X macro definitions (generates enums, name arrays, FFI exports from single source)
- MotionToKeys: Motion string → PhysicalKeys mapping, including `COUNT_SEARCHABLE_MOTIONS` for count-prefixed motion optimization
- SequenceTokenizer: Parses user input into motion tokens

**VimCore/VimUtils**: Implements Vim motion semantics (word motions w/e/b, paragraph {/}, sentence (/)

**VimCore/VimEditUtils**: Edit operations (deleteRange, insertText, joinLines, etc.) with minimal API design

**Optimizer/Config**: Keyboard layouts + cost models (presets: uniform, qwerty, colemak_dh)

**Optimizer/BufferIndex**: Pre-indexes buffer for landing positions (word/WORD begin/end, paragraph, sentence) enabling O(log n) count motion lookup

**Optimizer/Levenshtein**: Edit distance computation with prefix caching for A* heuristic (see below)

**Utils/Debug.h**: Debug output (enabled with `VIMFICIENCY_DEBUG` CMake option, ON by default)

### Neovim Plugin (lua/vimficiency/)

- **ffi.lua**: LuaJIT FFI bindings to C++ library
- **session.lua**: Manages optimization sessions (captures start/end snapshots)
- **session_store.lua**: Manages session storage across three types (manual, time-based, key-count)
- **key_tracking.lua**: Records user keypresses for comparison, filters non-motion mappings
- **simulate.lua**: Lua-side motion simulation for validation

### Key Session Filtering (key_tracking.lua)

Key-count sessions record every keypress to enable retroactive analysis. However, non-motion keys (like leader mappings triggering commands) should not be recorded. The filtering system in `key_tracking.lua` handles this.

**Detection mechanism:** When `vim.on_key` fires with `#typed > 1 && typed != key`, a multi-key mapping has fired. We inspect `maparg(typed)` to get the RHS and decide whether to filter.

**What we CAN reliably filter:**

| Pattern | Example | How Detected |
|---------|---------|--------------|
| Command-mode mappings | `<Space>w` → `:w<CR>` | RHS matches `^:` or `^<Cmd>` or `<C-u>:` |
| Vimficiency commands | `<Space>te` → `:Vimfy end 4<CR>` | RHS contains "Vimficiency" or "Vimfy" |

When filtered, we: (1) remove LHS keys from all sessions' `key_seq`, (2) remove sessions created by those LHS keypresses from `key_id_order`.

**What we CANNOT reliably filter:**

| Pattern | Example | Why Hard | Mitigation |
|---------|---------|----------|------------|
| Single-key → cmd | `Q` → `:q<CR>` | `#typed == 1`, no mapping detection triggers | Rare in practice |
| Non-cmd mappings | `gx` → open URL | RHS doesn't enter command mode | Creates useless session, evicted by FIFO |
| Aborted leader | `<Space>` then `<Esc>` | No mapping fires, can't detect abort | Extra session created |
| Lua function RHS | `<Space>x` → `function()...end` | RHS is function ref, not inspectable string | Depends on what function does |

**Potential future improvements (unexplored feasibility):**

1. **User-configurable leader key** - If user specifies leader (e.g., `<Space>`), aggressively filter sequences starting with it
2. **Pending state detection** - Check `vim.fn.state()` for typeahead to detect partial mappings
3. **Time-based heuristic** - Keys pressed rapidly before command-mode entry likely part of mapping
4. **Retroactive cleanup on mode change** - When entering command mode, remove recent sessions

**Related:** Screen-line motions `gj`/`gk` are converted to buffer-line equivalents `j`/`k` in `session.lua` via `APPROXIMATE_MOTION_CONVERSIONS` since the optimizer cannot model screen-relative positions.

## Build Artifacts

- `build/libvimficiency_core.a` - Static library (core logic)
- `build/libvimficiency.so` - Shared library (Lua FFI interface)
- `build/vimficiency_cli` - Standalone CLI
- `build/tests/vimficiency_tests` - GoogleTest suite

## Test Infrastructure

Uses **GoogleTest** (fetched via CMake FetchContent).

Test files in `data/TestFiles/` use naming convention:
- `a1_long_line.txt`, `a2_block_lines.txt`, `a3_spaced_lines.txt` - Abstract test cases
- `m1_main_basic.txt`, `m2_main_big.txt`, `m3_source_code.txt` - Realistic code snippets

The `TestUtils` class provides `TestFiles::load()` helper to read test files.

## Supported Motions

**Basic navigation:** h, j, k, l, 0, ^, $

**Word motions:** w, W, b, B, e, E, ge, gE

**Line jumps:** gg, G (with optional count for line number)

**Text objects:** {, }, (, )

**Character find:** f, F, t, T (with ;/, repeats)

**Scrolling:** `<C-d>`, `<C-u>`, `<C-f>`, `<C-b>`

**Count prefixes:** All above motions support counts (e.g., 3w, 5j, 10gg)

## Information on precise details of Vim Bindings. Important to know for implementation!

@~/.claude/docs/vim-motion.txt
@~/.claude/docs/vim-change.txt

## Important Constraints

**Cannot support** (due to minimal state representation):
- Screen-relative motions: gj, gk, H, M, L, zz
- Cross-buffer jumps
- Custom user mappings (currently only default Vim bindings)

**Current limitations** (from TODO.txt):
- No *, #, % symbol motions
- No search (/, n, N)
- No text objects (ci{, da", etc.)
- No visual mode

## Session Invocation Modes

Three ways to create optimization sessions, each with distinct aliasing (see `session_store.lua`):

1. **Manual** (aliases: a-e, capacity: 5) - Explicit `:Vimfy start <alias>` / `:Vimfy end <alias>`
2. **Time-based** (aliases: `.`, `..`, `...`, capacity: 5) - Auto-ends after configurable idle time (TODO: not yet implemented)
3. **Key Count Back** (aliases: 1-N, capacity: configurable) - Rolling FIFO of sessions; creates 1 session per keystroke, evicts oldest when full. Enables retroactive analysis (`:Vimfy end 4` = "what's optimal from 4 keystrokes ago?"). Enable with `:Vimfy key on`.

Memory bounded by storing only relevant line ranges (not full buffer).

## Edit Optimizer Heuristic (Optimizer/Levenshtein.h)

The edit optimizer uses A* search to find optimal Vim sequences for text transformations. The heuristic estimates "how far" the current buffer is from the goal buffer using Levenshtein edit distance.

### Why Levenshtein?

| Metric | Primitives | Vim mapping | Notes |
|--------|-----------|-------------|-------|
| **Levenshtein** | ins, del, sub | `i`/`a`, `x`, `r` | Substitute matches `r<char>` (2 keys) |
| LCS-based | ins, del only | `i`/`a`, `x` | Treats `a→b` as delete+insert (cost 2) |
| Damerau-Levenshtein | + transpose | + `xp` | Transpose is `xp` (2 keys anyway) |
| Hamming | substitute only | `r` | Requires equal length - rarely applicable |

Levenshtein is ideal because its substitution primitive directly maps to Vim's `r<char>` command.

### Why it works for A*

In A* with `priority = g(n) + h(n)`:
- After `dd` (cost 2): g += 2, h -= ~10 (if line had 10 chars) → **net: -8**
- After `x` (cost 1): g += 1, h -= 1 → **net: 0**

Bulk operations like `dd`, `cw`, `dw` make large dents in edit distance, so A* naturally explores them first. This is exactly what we want.

### Admissibility

For A* to find optimal solutions, h(n) must never overestimate actual cost:
- Levenshtein("a", "b") = 1
- Vim cost: at least 2 (`ra`)
- Since Levenshtein ≤ Vim cost, the heuristic is admissible ✓

### Performance: Prefix Caching

Naive approach: O(c²) Levenshtein per state × O(c²) states = O(c⁴) where c ≈ 50 chars.

Key insight: During A* search, consecutive states share prefixes (edits are local):
```
State S:  "hello world"
State S': "hello earth"  (edit at position 6)
          ^^^^^^ shared prefix - DP rows are identical
```

The `Levenshtein` class caches DP rows by prefix hash. For an edit at position p:
- Reuse cached rows 0..p-1
- Only compute rows p..n
- Effective complexity: O(suffix_length × goal_length) per query

### Multi-line Buffer Handling

Buffers are flattened to single strings with `\n` characters:
```cpp
flattenLines({"aaa", "bbb", "ccc"}) → "aaa\nbbb\nccc"
```

This correctly models Vim operations on newlines:
- `J`: Delete newline (join lines)
- `o`/`O`: Insert newline (open line)
- `dd`: Delete line including newline

Per-line distance would require complex alignment logic when line counts differ.

### Future Improvements (if needed)

- **Ukkonen's algorithm**: O(nd) where d = edit distance. Faster when d is small, which is common in A* (we prioritize close-to-goal states).
- **Myers' bit-vector**: O(nm/64) using SIMD-like bit parallelism. Requires goal length ≤ 64 for single-word optimization.

## Optimizer Architecture

### Three Optimizer Types

1. **MovementOptimizer**: Finds optimal motion sequence between two positions (same buffer)
   - A* search over position states
   - Supports all movement motions (word, f/F, counts, etc.)
   - Pre-indexes buffer for O(log n) count motion lookup

2. **EditOptimizer**: Finds optimal sequences to transform one text region to another
   - A* search over (buffer content, position, mode) states
   - Uses Levenshtein distance as heuristic
   - Outputs `EditResult.adj[i][j]` = optimal sequence from position i to j
   - Prunes invalid edits (e.g., word motions on empty lines) via `lineNonEmpty` guards

3. **CompositionOptimizer**: Composes movement + edits for multi-region changes
   - Uses Myers diff to identify edit regions
   - Pre-computes EditResult for each region
   - A* search over (position, editsCompleted) states

### CompositionOptimizer Design

**Initial Design (v1 - implemented):**
- State transitions: individual movement motions (j, k, h, l, etc.) + edit transitions
- Movement motions explored directly in main loop
- Simple but limited: only basic motions, no count/word/f motions

**Improved Design (v2 - planned):**
- Two edge types in search graph:
  1. **Edit edges**: Complete an edit (position in region X → position in region X, editsCompleted++)
  2. **Movement edges**: Move to next edit region (invoke MovementOptimizer on-demand)

- Key insight: Instead of exploring individual movement motions, delegate to MovementOptimizer
- Advantages:
  - Leverages existing MovementOptimizer (count motions, f/F, word motions already implemented)
  - On-demand computation: only compute movements for paths A* actually explores
  - Cleaner separation of concerns

- Algorithm:
  ```
  while pq not empty:
    state = pq.pop()
    if state.editsCompleted == totalEdits: yield result

    if pos in edit region for editsCompleted:
      for each valid (i,j) in EditResult.adj:
        explore edit transition

    if just finished edit (or at start):
      results = MovementOptimizer.optimize(currentPos → positions in next edit region)
      for each movement result:
        explore movement transition
  ```

- Considerations:
  - MovementOptimizer returns effort; need to integrate into CompositionState's RunningEffort
  - May call MovementOptimizer multiple times; could cache results if needed
  - Target positions: all valid starting positions in next edit region, or just entry points?

### Heuristic Design

**CompositionOptimizer heuristic:**
```
h(n) = suffixEditCosts[editsCompleted] + distanceToNextEditRegion
```

- `suffixEditCosts[i]` = pre-computed sum of min costs for edits i..n-1 (O(1) lookup)
- Uses min edit costs to heavily encourage completing edits
- Admissible: never overestimates actual cost

## Future Architecture (see plan files)

### Insert Mode Optimization (insert_mode_plan.txt)

Insert mode operations modeled as ordered diff sequences. For each diff `[start, end) → [new_start, new_end]`:

- **State expansion**: Search state includes `editsMade` counter and `original_pos` + `cumulate_edit_pos` for position tracking across edits
- **Single multi-source A\* search**: One optimizer run explores all valid start/end position pairs with insert/normal mode transitions, pruning unreachable pairs early
- **Strict boundaries**: Edit boundaries match exact diff ranges - motions that delete beyond boundaries are suboptimal (would require restoring deleted content)
- **Sequential assumption**: Edits processed in document order; heavy penalty for positions past current edit range

## X Macros Pattern

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

## Edit System Design (Editor/Edit.cpp, VimCore/VimEditUtils)

### Design Principles

1. **Assume valid state** - Use assertions, not defensive clamping. Invalid states indicate bugs in search logic.

2. **Minimal API** - Single-line operations take `string& line, int& col`, not `Lines& + Position&`.

3. **Upfront guards** - Handle edge cases (empty buffer, empty line) at function entry, then assume valid preconditions.

4. **Strict count validation** - Operations with deterministic outcomes (`x`, `X`, `~`, `r`, `s`, `dd`, `J`) throw if count exceeds available chars/lines. Search should prune these.

### Empty State Handling

Symmetric treatment of empty containers:

| Container | Empty state | Index | Guard pattern |
|-----------|-------------|-------|---------------|
| `line` (string) | `""` | `col = 0` | `if (line.empty()) return;` |
| `lines` (vector) | `{}` | `line = 0, col = 0` | `if (lines.empty()) { pos = {0,0}; return; }` |

This allows distinguishing "one empty line" from "no lines" for diff purposes.

### Position and targetCol

`Position` has three fields: `line`, `col`, `targetCol`. Use appropriately:

- `pos.setCol(c)` - Updates both `col` and `targetCol` (for horizontal movements)
- `pos.col = c` - Updates only `col`, preserves `targetCol` (for vertical movements restoring from targetCol)

Per [Vim's motion.txt](https://vimhelp.org/motion.txt.html), linewise operations like `dd` go to first non-blank and update targetCol (due to `'startofline'` default).

### Supported Edit Operations

**Normal mode:**
- Character: `x`, `X`, `s`, `r{char}`, `~`
- Word: `dw`, `de`, `db`, `dge`, `dW`, `dE`, `dB`, `dgE` (and `c` variants)
- Line: `dd`, `cc`, `S`, `D`, `C`, `d$`, `c$`, `d0`, `c0`, `d^`, `c^`
- Join: `J`, `gJ`
- Open: `o`, `O`
- Mode entry: `i`, `I`, `a`, `A`

**Insert mode:**
- `<Esc>`, `<BS>`, `<Del>`, `<CR>`
- `<C-u>` (delete to line start), `<C-w>` (delete word)
- Arrow keys: `<Left>`, `<Right>`, `<Up>`, `<Down>`

## Debug Mode

CMake option `VIMFICIENCY_DEBUG` (default ON) enables debug output:
- Defines `VIMFICIENCY_DEBUG` preprocessor macro
- Use `debug(...)` from Utils/Debug.h
- Output captured via `consume_debug_output()`
