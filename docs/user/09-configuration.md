**[← Keymaps](08-keymaps.md)** | **[Index](./README.md)** | **[Next: Effort model →](10-effort-model.md)**

---

# 9. Configuration

All settings are optional. Pass a table to `setup`:

```lua
require('vimficiency').setup({
  -- Recall queue (bounded). Union-semantic retention: a session is kept
  -- as long as EITHER cap still holds it, and evicted only when BOTH
  -- say drop. So raise either cap to keep more — lower both to trim.
  KEY_SESSION_CAPACITY = 200,  -- count floor (sessions). Default 200.
  MAX_RETENTION_SECONDS = 120, -- age floor (seconds). Default 120.

  -- Suggest (see page 6 for full semantics)
  auto_suggest = {
    idle = { ms = 3000, window = "3s" },
    -- keys and cost triggers: reserved, not yet implemented
  },

  -- Watch (see page 4). Independent from `auto_suggest`; share the same
  -- idle engine but with their own thresholds.
  watch = {
    idle_ms = 3000,     -- auto-end after N ms of real keystroke idleness
    cooldown_ms = 5000, -- minimum time between two auto-fires
  },

  -- Search region around the edit
  SLICE_PADDING = 5,                 -- lines of context above/below
  MAX_SEARCH_LINES = 500,            -- refuse to optimize bigger regions
  SLICE_EXPAND_TO_PARAGRAPH = false, -- extend slice to paragraph bounds

  -- Result count
  RESULTS_CALCULATED = 20, -- optimizer internal capacity
  RESULTS_SAVED = 5,       -- how many suggestions kept per session

  -- (effort-model knobs are also accepted; see below)
})
```

Unknown keys produce a loud warning at setup — typos don't silently no-op.

## Inspect what the C++ layer is using

```vim
:Vimfy config
```

Opens a scratch buffer with the full current configuration, including the
effort-model values that aren't listed above.

## Shiftwidth

Your Neovim `shiftwidth` is auto-detected at setup time and passed to the
optimizer so suggested sequences match your indentation. To override
explicitly:

```lua
require('vimficiency').setup({ shiftwidth = 4 })
```

## Effort model tuning

The optimizer scores sequences by a keyboard-effort model with several
tunable knobs: per-key base costs, same-finger / same-key / alt-hand /
good-roll / bad-roll weights, and count-penalty overrides. These are
accepted in `setup{}` but documented separately — see
[10. The effort model](10-effort-model.md).

---

**[← Keymaps](08-keymaps.md)** | **[Index](./README.md)** | **[Next: Effort model →](10-effort-model.md)**
