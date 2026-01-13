### Key Session Filtering (key_tracking.lua)

**Key filtering:** `key_tracking.lua` filters non-motion keys from sessions. Multi-key mappings (where `#typed > 1 && typed != key`) are detected via `maparg(typed)` — we filter if RHS matches `^:`, `^<Cmd>`, or contains "Vimfy". Single-key remaps and lua-function RHS cannot be reliably detected.

**Motion approximation:** Screen-line motions `gj`/`gk` are converted to `j`/`k` via `APPROXIMATE_MOTION_CONVERSIONS` in `session.lua` since the optimizer can't model screen-relative positions.

### Neovim Plugin (lua/vimficiency/)

- **ffi.lua**: LuaJIT FFI bindings to C++ library
- **session.lua**: Manages optimization sessions (captures start/end snapshots)
- **session_store.lua**: Manages session storage across three types (manual, time-based, key-count)
- **key_tracking.lua**: Records user keypresses for comparison, filters non-motion mappings
- **simulate.lua**: Lua-side motion simulation for validation

## Session Invocation Modes

Three ways to create optimization sessions, each with distinct aliasing (see `session_store.lua`):

1. **Manual** (aliases: a-e, capacity: 5) - Explicit `:Vimfy start <alias>` / `:Vimfy end <alias>`
2. **Time-based** (aliases: `.`, `..`, `...`, capacity: 5) - Auto-ends after configurable idle time (TODO: not yet implemented)
3. **Key Count Back** (aliases: 1-N, capacity: configurable) - Rolling FIFO of sessions; creates 1 session per keystroke, evicts oldest when full. Enables retroactive analysis (`:Vimfy end 4` = "what's optimal from 4 keystrokes ago?"). Enable with `:Vimfy key on`.
