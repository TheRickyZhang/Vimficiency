---
title: "Suggest (auto start, auto end)"
---

# Suggest (auto start, auto end)

Suggest runs the optimizer on its own and surfaces a result without
you calling `:Vimfy finish` or `:Vimfy recall`. It reuses the recall queue
for the start and fires on triggers you configure.

## Zero-config default

You don't need to configure anything to try it:

```vim
:Vimfy suggest on
```

With no `auto_suggest` block in `setup{}`, Suggest arms a built-in **cost
gate** (`auto_suggest.default_cost`, default
`{ m = 1.5, b = 2.0, ms = 1000, window = "3s" }`): after a brief idle pause it
analyzes the last ~3s of editing and surfaces a result only when your typed
effort exceeds `1.5 * optimal + 2`. The toast is one line —
`<your effort> → <optimal>  <suggested keys>` — and the result is already
saved as a session, reachable with the `@` selector:

```vim
:Vimfy explore @        " inspect interactively
:Vimfy play @           " replay side-by-side
:Vimfy save @ <name>    " persist to disk
```

To tune this gate, set `auto_suggest = { default_cost = { ... } }` (any subset
merges over the defaults) — this alone does not auto-enable at startup. To
auto-enable at startup, add a real trigger (`idle`/`keys`/`cost`, below).
Setting `auto_suggest = false` turns the feature off entirely, and
`:Vimfy suggest on` will refuse.

## Configured triggers

```lua
require('vimficiency').setup({
    auto_suggest = {
        idle = { ms = 3000, window = "3s" },
        keys = { every = 50 },              -- every 50 keystrokes
        cost = { m = 1.5, b = 2.0, ms = 300, window = "100" },
        cooldown_ms = 5000,                 -- feature-level, applies to all
    },
})
```

Any subset of triggers may be configured; presence of the sub-table
enables the trigger. Omit the sub-table to disable that trigger.

Because Suggest is (auto, auto), both halves have a config pointer:

- **Start scope** (`window`) — slice of the recall queue to analyze.
  Grammar + semantics: see [Window aliases](05-recall.md#window-aliases-two-ways-to-index-the-queue).
  `keys` has no separate window knob — the analysis slice is always
  `every` keystrokes.
- **End trigger** (`ms`, `every`, `cooldown_ms`) — when Suggest fires.
  Idle/cost share the end-detection policy with Watch: see [Idle
  end-detection](09-configuration.md#idle-end-detection).

## Triggers

| Trigger | Fires when                                                                          |
|---------|-------------------------------------------------------------------------------------|
| `idle`  | You've been idle for `idle.ms` milliseconds.                                        |
| `keys`  | Every `keys.every` real keystrokes.                                                 |
| `cost`  | After `cost.ms` idle, iff `user_cost > cost.m * optimal + cost.b`.                   |

### `idle = { ms, window }`

Fires on a pure pause. `window` is the analysis slice; specify the full
trigger object when `idle` is present.

### `keys = { every }`

Fires every `every` real user keystrokes. The analysis slice is always
the last `every` keystrokes — a separate `window` would collapse to
the same value, so it's not a knob.

### `cost = { m, b, ms, window }`

The most selective trigger: only surfaces a result when your typed
cost exceeds the optimal by a configurable margin. Because it runs
the optimizer, it's idle-gated on `cost.ms` — the check only runs at
micro-pauses between commands, not on every keystroke.

> **v1 caveat.** Cost runs the optimizer synchronously on the idle
> tick. With `idle.ms = 3000` and `cost.ms = 300` armed together a 3s
> pause will re-run the analyzer every 300ms until the cooldown
> elapses. Acceptable for v1; the ADR describes a debounced-subprocess
> plan if this shows up as a measured cost.

## Dedup

Beyond `cooldown_ms`, Suggest also fingerprints the computed result
(edit region + user sequence + top suggestion) and suppresses a fire
whose result matches the previously shown one. The fingerprint resets
when you disable Suggest.

For `cooldown_ms` and the `idle.ms` threshold itself, see [Idle
end-detection](09-configuration.md#idle-end-detection) — Watch and
Suggest share that policy.

## Runtime on/off

```vim
:Vimfy suggest on        " enable (uses configured triggers, else the default cost gate)
:Vimfy suggest off       " suppress firing; config untouched
:Vimfy suggest toggle
```

If `setup{}` provides an `auto_suggest` block with triggers, Suggest is
enabled automatically at startup. With no block, it stays off until you run
`:Vimfy suggest on`, which falls back to the zero-config cost gate above.

## Relationship to the recall queue

Suggest queries the same queue as [Recall](05-recall.md). Recall is
always on, so there's nothing to configure for it separately.
