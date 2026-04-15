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

  -- Suggest (see page 6 for full semantics). Three triggers; enable any
  -- subset. If a trigger is present, specify the full trigger object.
  auto_suggest = {
    idle = { ms = 3000, window = "3s" }, -- fire after 3s idle
    keys = { every = 50 },               -- fire every 50 keystrokes
    cost = { m = 1.5, b = 2.0, ms = 300, window = "100" },
    cooldown_ms = 5000,                  -- feature-level, applies to all triggers
  },

  -- Watch (see page 4). Shape parallels `auto_suggest` — same `idle`
  -- nesting with `ms`. `idle.window` is rejected because Watch starts
  -- manually. Independent config; both can run at once with different
  -- thresholds.
  watch = {
    idle = { ms = 3000 },
    cooldown_ms = 5000,
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

## Idle end-detection

Two features auto-fire on keystroke idleness: [Watch](04-watch.md)
(ends a manual session once you pause) and [Suggest](06-suggest.md)'s
`idle` / `cost` triggers (run the optimizer on a window of the recall
queue). Both use the same engine but run independently — each owns
its own timer and cooldown, configured under its own key.

| Parameter                                        | Meaning                                                                |
|--------------------------------------------------|------------------------------------------------------------------------|
| `watch.idle.ms` / `auto_suggest.idle.ms`         | Real keystroke idleness, in ms, before the trigger fires.              |
| `auto_suggest.cost.ms`                           | Same semantic as `idle.ms` but dedicated to the cost trigger.          |
| `watch.cooldown_ms` / `auto_suggest.cooldown_ms` | Minimum time between consecutive fires. Safety rail for low `idle.ms`. |

"Real keystroke" excludes cmdline input and Vimficiency admin
keystrokes, so `:Vimfy` commands and `vimfy.map`-routed bindings don't
reset the idle timer.

A bare `ms` inside any trigger object universally means "idle
threshold" — fire only after N ms of no activity. `keys.every` is the
one exception: it counts keystrokes, not time, so it has no `ms`.

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
