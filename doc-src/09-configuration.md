---
title: "Configuration"
---

# Configuration

All settings are optional. For the canonical full setup, including the config
shape and recommended keymaps, see
[`examples/config.lua`](../examples/config.lua).

Minimal setup:

```lua
require('vimficiency').setup()
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

"Real keystroke" excludes cmdline input and Vimfy admin
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
