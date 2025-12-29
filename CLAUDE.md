# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Vimficiency is a Vim motion optimizer that analyzes cursor movements and recommends more efficient key sequences. The system uses A* search with keyboard-aware cost modeling to find optimal Vim motions between editor positions.

**Architecture:** C++ core library + LuaJIT FFI bridge + Neovim plugin

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

Key constraint: Search uses **minimal state** (contiguous buffer lines + position only), which means screen-relative motions (H, M, L, zz, gj, gk) and cross-buffer jumps are not supported.

### Motion Processing Pipeline

1. **MotionToKeys** (Keyboard/): Maps Vim motion strings (e.g., "w", "3j", "gg") to KeySequence
2. **KeySequence**: Vector of Key enum values representing physical keypresses
3. **Config** (Optimizer/): Defines keyboard layout (QWERTY/Colemak-DH/Uniform) with per-key costs and hand/finger assignments
4. **Motion application** (Editor/Motion.h): Applies motion semantics to Position, returns new Position + Mode

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

**Editor/**: Position, Snapshot (buffer + cursor state), Motion (parsing and application), Mode enum

**State/**: State struct (Position + RunningEffort + cost + sequence), RunningEffort (tracks typing effort patterns)

**Keyboard/**:
- KeyboardModel.h: Key/Hand/Finger enums, KeyInfo struct
- XMacroKeyDefinitions.h: X macro definitions (generates enums, name arrays, FFI exports from single source)
- MotionToKeys: Motion string → KeySequence mapping
- SequenceTokenizer: Parses user input into motion tokens

**VimCore/VimUtils**: Implements Vim motion semantics (word motions w/e/b, paragraph {/}, sentence (/)

**Optimizer/Config**: Keyboard layouts + cost models (presets: uniform, qwerty, colemak_dh)

**Utils/Debug.h**: Debug output (enabled with `VIMFICIENCY_DEBUG` CMake option, ON by default)

### Neovim Plugin (lua/vimficiency/)

- **ffi.lua**: LuaJIT FFI bindings to C++ library
- **session.lua**: Manages optimization sessions (captures start/end snapshots)
- **key_tracking.lua**: Records user keypresses for comparison
- **simulate.lua**: Lua-side motion simulation for validation

## Build Artifacts

- `build/libvimficiency_core.a` - Static library (core logic)
- `build/libvimficiency.so` - Shared library (Lua FFI interface)
- `build/vimficiency_cli` - Standalone CLI
- `build/tests/vimficiency_tests` - GoogleTest suite

## Test Infrastructure

Uses **GoogleTest** (fetched via CMake FetchContent).

Test files in `data/TestFiles/` use naming convention:
- `a1_long_line.txt`, `a2_block_lines.txt` - Abstract test cases
- `m1_main_basic.txt` - Realistic code snippets

The `TestUtils` class provides `TestFiles::load()` helper to read test files.

## Important Constraints

**Cannot support** (due to minimal state representation):
- Screen-relative motions: gj, gk, H, M, L, zz
- Cross-buffer jumps
- Custom user mappings (currently only default Vim bindings)

**Current limitations** (from TODO.txt):
- No f/t/;/, character motions yet
- No search (/, n, N)
- No count prefixes (e.g., 3w, 5j)
- No operator motions (dw, ci{)
- No insert mode operations
- No visual mode

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

## Debug Mode

CMake option `VIMFICIENCY_DEBUG` (default ON) enables debug output:
- Defines `VIMFICIENCY_DEBUG` preprocessor macro
- Use `debug(...)` from Utils/Debug.h
- Output captured via `get_debug_output()` / `clear_debug_output()`
