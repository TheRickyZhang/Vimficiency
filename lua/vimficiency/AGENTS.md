# Lua Layer Documentation

The Lua layer is a Neovim plugin that captures user editing sessions and calls the C++ optimizer for analysis.

## File Overview

| File | Purpose |
|------|---------|
| `init.lua` | Plugin entry point. Registers `:Vimfy` command with subcommands. |
| `config.lua` | Shared configuration constants (Lua-side only). |
| `ffi.lua` | LuaJIT FFI bindings to `libvimficiency.so`. |
| `session.lua` | Session lifecycle: start, finish, simulate, view. |
| `session_store.lua` | Canonical session records + manual/recall indexing. |
| `result_view.lua` | Pure formatting helpers (position string, body lines) shared by `finish` and auto-suggest. |
| `auto_suggest.lua` | Idle-trigger auto-suggest (runs optimizer on a recall window). |
| `key_tracking.lua` | Captures keypresses via `vim.on_key()` with filtering. |
| `simulate.lua` | Side-by-side animation of motion sequences. |
| `util.lua` | State capture, ID generation, UI helpers. |

## Session Architecture

### Session Types (session_store.lua)

1. **Manual** (aliases: arbitrary alphabetic strings — e.g. `a`, `refactor`, `bugfix`;
   capacity: 5 concurrent active sessions)
   User-controlled: `:Vimfy start <name>` / `:Vimfy end <name>`. Grammar is
   strictly alphabetic (`^%a+$`); recall forms are rejected at the `end`
   entry point with a redirect to `:Vimfy recall`.

2. **Recall** (rolling ring; queried via `:Vimfy recall N` or `:Vimfy recall Ns`)
   Creates a retained session on every keystroke. Both indexing modes share
   one ring — `N` means "N keys ago", `Ns` means "as far back as N seconds
   ago, snapped backward to a clean normal-mode command boundary".
   Permanently on (installed by `session_store.install_recall()` at
   setup). Retention is bounded under **union** semantics: evict the
   oldest session only when BOTH `KEY_SESSION_CAPACITY` and
   `MAX_RETENTION_SECONDS` say drop.

### Data Structures

- **SessionRecord**: Canonical per-session entry with `status` ("active" | "finished"),
  key event buffer (while active), `first_mode` (captured on recall creation for
  command-boundary snapping), and `result` (after finish).
- **ResultSession**: Optimizer output + position + captured `start_time`/`key_count`,
  attached to the record at finish.
- **SessionSummary**: Normalized view-model for `:Vimfy list` / auto-suggest output.

One `session_records[id]` table holds everything; `manual_alias_to_id` and
`recall_id_order` are the two indexes into it. Finish flips `status` in place
and drops `key_seq` — it does NOT unindex. This removes tombstones from
`recall_id_order` entirely: a finished session stays reachable via its recall
alias (for `:Vimfy sim Ns`) until the ring slot rotates out.

## Key Tracking (key_tracking.lua)

Uses `vim.on_key(callback)` to capture keypresses with post-processing deduplication.

### Deduplication Logic

When an operator-pending command completes (e.g., `cw`), Neovim re-evaluates and fires the
motion key twice. `build_sequence()` removes these duplicates by detecting:
- Same key appearing twice in succession
- First occurrence in operator-pending mode (`no`)
- Second occurrence in different mode (normal/insert)

### Filtering logic
Announce-only: Vimfy entry points (the `:Vimfy` user command, `<Plug>Vimfy*`
maps, `M.wrap(fn)`) bracket their bodies with `key_tracking.begin_ignore` /
`end_ignore`. Both `on_key` handlers short-circuit when the flag is set,
so admin-triggered key events are never recorded. Save/restore via the
returned `prev` keeps nested invocations safe.

Cmdline activity (`m == "c"`, `:` in normal mode) is dropped unconditionally
as meta-activity, independent of the flag.

No retroactive cleanup, no RHS/maparg heuristics. User mappings that invoke
Vimfy without routing through `<Plug>` or `wrap()` (e.g. `nnoremap X
:Vimfy start a<CR>`) will have the LHS keypress counted as motion — this
is documented and the user fix is to migrate the mapping.

### Known limitations
- Text object final character missing (`ciw` → `c, i, ?`) - Neovim consumes internally before `vim.on_key` fires
- See `dev/lua/neovim_on_key_issues.md` for detailed analysis and potential workarounds
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
:Vimfy end <alias>      -- Finish a manual session (Mark/Watch)
:Vimfy recall <N|Ns>    -- Finish a retrospective recall window
:Vimfy close <alias>    -- Discard session
:Vimfy sim <alias>      -- Animate results
:Vimfy view [name]      -- View saved results
:Vimfy list             -- Show active/saved sessions
:Vimfy suggest <on|off|toggle> -- Toggle auto-suggest (needs `auto_suggest = {...}` in setup)
:Vimfy config           -- Show C++ config state
:Vimfy reload           -- Rebuild C++ library
```
