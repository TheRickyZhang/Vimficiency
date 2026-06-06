---
title: "Configuration"
---

# Configuration

All settings are optional, and this page documents each one in detail. For a
copy-pasteable starting point with recommended keymaps, see
[`examples/config.lua`](../examples/config.lua) — it sets the common toggles at
their defaults and points back here for the advanced knobs.

Minimal setup:

```lua
require('vimficiency').setup()
```

Unknown keys produce a loud warning at setup — typos don't silently no-op.

## Search and performance

The optimizer runs **synchronously on Neovim's main thread**, so each run
blocks the editor until it returns. These knobs bound how much work a run
does — reach for them first if [Suggest](06-suggest.md) or interactive Explore
ever feels laggy. All are optional and default as shown.

| Field | Default | Meaning |
|-------|---------|---------|
| `MAX_SEARCH_LINES` | `500` | Hard ceiling on the analyzed slice height. A region taller than this is skipped entirely instead of analyzed — the strongest guard against a runaway search. Lower it to cap worst-case latency. |
| `SLICE_PADDING` | `5` | Context lines included above and below the detected edit region before searching. More padding means a larger search. |
| `SLICE_EXPAND_TO_PARAGRAPH` | `false` | When `true`, grows the analyzed slice out to the surrounding paragraph boundaries. Off keeps slices tight. |
| `RESULTS_CALCULATED` | `20` | How many candidate sequences the search computes per run. |
| `RESULTS_SAVED` | `5` | How many of those candidates are kept and shown. |

## Optimizer overrides

`optimizer = { ... }` forwards low-level A\* parameters to **every** optimizer
call (mark, watch, recall, suggest, and explore defaults). Leave it empty to
use the built-in defaults; set only the keys you want to change. Confirm the
values actually in effect with `:Vimfy config`.

| Key | Default | Meaning |
|-----|---------|---------|
| `maxNodesPopped` | `50000` | A\* search budget — the most states the search will expand before stopping. The main lever for per-run cost; lower it to trade thoroughness for speed. |
| `maxResults` | `20` | Upper bound on results retained inside the search. |
| `effortWeight` | `1.0` | Weight on keystroke effort in the cost function. |
| `distanceWeight` | `1.0` | Weight on the distance-to-goal heuristic. Set to `0` for an exact Dijkstra search — guaranteed cheapest result, but slower. |
| `exploreFactor` | `2.0` | Effort cutoff multiplier: the search only keeps candidate sequences whose effort is at most `exploreFactor ×` your typed effort (default `2.0` = up to twice as costly). Lower it to prune the search harder (faster, but may discard distant alternatives). |
| `minPrefixCount` / `maxPrefixCount` | `4` / `16` | Range of count prefixes the search tries (e.g. `3w`). Setting `minPrefixCount > maxPrefixCount` disables count-prefixed exploration while leaving unprefixed search intact. |

The distance heuristic is intentionally inadmissible (it can overestimate), so
a higher `distanceWeight` searches faster but may miss the cheapest sequence;
`distanceWeight = 0` recovers optimality at the cost of speed.

## Recall and session safety

[Recall](05-recall.md) keeps a rolling ring of recent keystrokes so you can
analyze "N keys / Ns ago" after the fact. These knobs bound that ring and the
lifetime of in-progress sessions.

| Field | Default | Meaning |
|-------|---------|---------|
| `KEY_SESSION_CAPACITY` | `200` | Maximum keystrokes retained in the recall ring. |
| `MAX_RETENTION_SECONDS` | `120` | Maximum age of recall history. The oldest entries are evicted once **both** the capacity and the age limit are exceeded. |
| `MANUAL_IDLE_TIMEOUT_SECONDS` | `300` | A manual (`start`/`finish`) session left idle this long is auto-ended with a warning. |
| `SNAP_LOOKBACK_KEYS` | `20` | When resolving a recall window, snap its start to a nearby clean key boundary lying within this many keys. |

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
