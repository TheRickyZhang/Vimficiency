# Lua Layer Documentation

The Lua layer is a Neovim plugin that captures user editing sessions and calls the C++ optimizer for analysis.

## File Overview

| File | Purpose |
|------|---------|
| `init.lua` | Plugin entry point. Registers `:Vimfy` command with subcommands. |
| `config.lua` | Shared configuration constants (Lua-side only). |
| `ffi.lua` | LuaJIT FFI bindings to `libvimficiency.so`. |
| `session.lua` | Session lifecycle: start, finish, simulate, view. |
| `session_store.lua` | Storage management for active/result sessions across three types. |
| `key_tracking.lua` | Captures keypresses via `vim.on_key()` with filtering. |
| `simulate.lua` | Side-by-side animation of motion sequences. |
| `util.lua` | State capture, ID generation, UI helpers. |

## Session Architecture

### Session Types (session_store.lua)

1. **Manual** (aliases: `a`-`e`, capacity: 5)
   User-controlled: `:Vimfy start a` / `:Vimfy end a`

2. **Time-based** (aliases: `.`, `..`, capacity: 5)
   TODO: Not implemented. Auto-end after idle time.

3. **Key-count** (aliases: `1`, `2`, ..., capacity: configurable)
   Rolling FIFO: creates session per keystroke, enabling retroactive analysis.
   Enable: `:Vimfy key on`. Query: `:Vimfy end 4` (4 keys ago).

### Data Structures

- **ActiveSession**: In-progress session with key event buffer.
- **ResultSession**: Completed session with optimal sequences.

Sessions transition from active to result on `finish()`. Storage uses separate tables: `active_sessions[id]` and `result_sessions[id]`.

## Key Tracking (key_tracking.lua)

Uses `vim.on_key(callback)` to capture keypresses with post-processing deduplication.

### Deduplication Logic

When an operator-pending command completes (e.g., `cw`), Neovim re-evaluates and fires the
motion key twice. `build_sequence()` removes these duplicates by detecting:
- Same key appearing twice in succession
- First occurrence in operator-pending mode (`no`)
- Second occurrence in different mode (normal/insert)

### Filtering logic
- Command-line mode (`:` prefix, `<Cmd>`) is excluded
- Multi-key mappings with filtered RHS (Vimfy commands) trigger removal of accumulated LHS keys
- Single-key remaps and lua-function RHS cannot be reliably detected

### Known limitations
- Text object final character missing (`ciw` → `c, i, ?`) - Neovim consumes internally before `vim.on_key` fires
- See neovim/neovim#19426 for `v:motion` feature request that would help

**Approximate motion conversions** (in session.lua):
- `gj` → `j`, `gk` → `k` (screen-line motions unsupported by optimizer)

## FFI Interface (ffi.lua)

Loads `libvimficiency.so` via LuaJIT FFI. Key exports:

```lua
ffi_lib.analyze(lines, ...) → VimficiencyResult[], debug_str
ffi_lib.configure(user_config)  -- Push config to C++
ffi_lib.tokenize_motions(seq) → string[]
```

**Position indexing:** C++ uses 0-indexed rows/cols. Neovim uses 1-indexed rows. Conversion happens at FFI boundary in `session.lua`.

## Configuration (config.lua)

Lua-side constants (not pushed to C++):

| Setting | Default | Description |
|---------|---------|-------------|
| `RESULTS_CALCULATED` | 20 | Max results from optimizer |
| `RESULTS_SAVED` | 5 | Results kept per session |
| `SLICE_PADDING` | 5 | Lines above/below cursor for search slice |
| `MAX_SEARCH_LINES` | 500 | Max lines in search region |
| `KEY_SESSION_CAPACITY` | 10 | Rolling key session limit |

## Simulation (simulate.lua)

`simulate_compare(lines, row, col, sequences, delay_ms)`:
- Opens new tab with side-by-side windows
- Animates each sequence step-by-step
- Uses C++ tokenizer with character fallback for unsupported motions
- Press `q` to close simulation

## Commands

```
:Vimfy start <alias>    -- Start manual session
:Vimfy end <alias>      -- Finish and optimize
:Vimfy close <alias>    -- Discard session
:Vimfy sim <alias>      -- Animate results
:Vimfy view [name]      -- View saved results
:Vimfy list             -- Show active/saved sessions
:Vimfy key <on|off>     -- Toggle key-count sessions
:Vimfy config           -- Show C++ config state
:Vimfy reload           -- Rebuild C++ library
```
