**[← Recall](04-recall.md)** | **[Index](./README.md)** | **[Next: Results →](06-results.md)**

---

# 5. Auto-suggest

Auto-suggest runs the optimizer on its own and surfaces a result without
you calling `:Vimfy end`. It's orthogonal to the session kinds in
[2. Sessions](02-sessions.md) — it reuses the recall ring but decides
*when* to analyze it based on triggers you configure.

## Shape

```lua
require('vimficiency').setup({
    auto_suggest = {
        idle = { ms = 3000, window = "3s" },
        cooldown_ms = 5000,                       -- suppress rapid re-fires
        -- keys = { every = 50, window = "50" },  -- future, not yet implemented
        -- cost = { m = 1.5, b = 2.0 },           -- future, not yet implemented
    },
})
```

- Setting `auto_suggest = false` (or omitting it) disables auto-suggest
  entirely.
- Each trigger is its own sub-table. **Presence of the sub-table means
  that trigger is enabled.** No `enabled = true`, no sentinel values.
- Multiple triggers compose as OR — any trigger firing surfaces the
  result.

The validator errors loudly if you name a trigger that doesn't exist
yet (`keys`, `cost`). This reserves the names so typos can't silently
no-op.

## Triggers

### `idle` — fire after a pause *(shipping)*

Fires when you've been idle for `ms` milliseconds. Analyzes the last
`window` of the ring (a recall alias: `"3s"` or `"50"` for 50 keys).

```lua
auto_suggest = { idle = { ms = 3000, window = "3s" } },
```

This is the simplest trigger and the only one implemented right now.
Works well as a "tell me when I pause" notification.

### `keys` — fire every N keys *(future)*

Will fire every `every` user keystrokes since the previous auto-suggest.
Useful if you want constant feedback and don't mind the optimizer
running often.

### `cost` — fire only when you were wasteful *(future)*

Will fire only when the optimizer finds you used more effort than
necessary: `user_cost > m * optimal_cost + b`. This is the most
interesting trigger because it surfaces signal, not noise — you only
hear from Vimficiency when you actually had a better option.

The cost trigger will run asynchronously (in a subprocess) to avoid
blocking Neovim while the optimizer computes, and will be debounced
to roughly one check per ~300ms of activity. See the
architecture-decision-record for details.

## Dedup and cooldown

Auto-suggest suppresses a fire if either:

- The result is *identical* to the last one surfaced (same user
  sequence, same starting buffer, same top suggestions), or
- Less than `cooldown_ms` has elapsed since the last fire.

Fingerprinting catches "I idled twice at the same spot" — you don't
see the same recommendation echoed. Cooldown rate-limits rapid
retriggers from adjacent pauses. Default cooldown is 5 seconds.

## Runtime on/off

You can override the config at runtime:

```vim
:Vimfy suggest on        " Enable if config has triggers
:Vimfy suggest off       " Disable without editing config
:Vimfy suggest toggle
```

`suggest off` does not remove the config; it just suppresses firing.
Next `suggest on` restores the configured behavior.

## How it relates to the recall ring

Auto-suggest queries the same ring as [4. Recall](04-recall.md). The
ring must be recording for auto-suggest to have anything to analyze.
By default, enabling `auto_suggest` in `setup{}` also turns on recall
automatically.

---

**[← Recall](04-recall.md)** | **[Index](./README.md)** | **[Next: Results →](06-results.md)**
