**[← Manual](03-manual.md)** | **[Index](./README.md)** | **[Next: Auto-suggest →](05-auto-suggest.md)**

---

# 4. Recall (retrospective, "N keys / seconds ago")

Recall lets you look backward: "that thing I just did — was there a
better way?" You don't pre-mark anything. A rolling ring captures every
keystroke, and you query it after the fact.

```vim
:Vimfy recall on      " Start populating the ring
" ... edit normally ...
:Vimfy end 6          " Analyze the last 6 keystrokes
:Vimfy end 3s         " Analyze the last 3 seconds
```

## Two ways to index the ring

| Syntax           | Meaning                             | Use when                                        |
|------------------|-------------------------------------|-------------------------------------------------|
| `:Vimfy end 6`   | Last 6 keystrokes                   | You can estimate how many keys the edit took    |
| `:Vimfy end 3s`  | Last 3 seconds of activity          | You can estimate time better than key count     |

The alias grammar is deliberate:

- Digits only → key-count recall.
- Digits followed by `s` → time recall.
- Alphabetic → manual session name.

Mixing forms (`3m`, `3ms`, `2h`, etc.) is not supported. Seconds is the
only time unit; if you want 90 seconds, use `90s`.

## How time recall works

Each keystroke is tagged with a timestamp when captured. `:Vimfy end Ns`
resolves to "the keystroke count `K` such that the `K`th-to-last key
was typed within the last `N` seconds" and then behaves like
`:Vimfy end K`.

**Command-boundary snapping.** A raw time window could land mid-command
(e.g., between `d` and `aw`, or mid-count-prefix like `2` of `2dw`).
The window start is **snapped backward** to include the full normal-mode
command that contains the cutoff, up to a bounded lookback (a handful
of keys). Insert-mode content isn't snapped — "in insert mode at
cutoff" is a valid start state and the optimizer analyzes from there.

If no identifiable command boundary exists within the bounded
lookback, the command fails with a clear message ("couldn't identify
a complete command in the last Ns; try a larger window or `end N` by
key count").

The end boundary is always "now" — presumed clean because you just
typed `:Vimfy end`.

## Known behaviors of time recall

- **Time windows can straddle idle gaps.** If you thought for 10
  seconds mid-edit, `3s` will only see keys from after the thinking
  pause. This is usually what you want.
- **Fast bursts fit a lot into short windows.** `3s` of rapid typing
  can be 200+ keystrokes. The optimizer will analyze the whole window;
  budget yourself accordingly if the edit is big.
- **First keypress of a `<Plug>`-mapped LHS.** Due to how `vim.on_key`
  fires, the last raw key of a `<Plug>`-mapped LHS may be captured
  before the mapping resolves. Either use time recall (`Ns` rolls past
  that single key without noticing) or count `N` with the admin
  keypress in mind. See [12. Limitations](12-limitations.md).

## Capacity

The ring is bounded by two caps, applied with **union semantics** — a
session is kept as long as EITHER cap still holds it, and only evicted
when BOTH say drop:

| Knob                       | Default | Meaning                                |
|----------------------------|---------|----------------------------------------|
| `KEY_SESSION_CAPACITY`     | 200     | Count floor: keep at least N sessions. |
| `MAX_RETENTION_SECONDS`    | 120     | Age floor: keep sessions newer than N. |

So at defaults, the ring always retains at least 200 sessions AND at
least the last 120 seconds of activity. During a burst of fast typing
you'll exceed 200 within 120s and the count floor keeps the older
sessions; during slow typing you'll fall under 200 and the age floor
keeps sessions from aging out. You need to cross *both* thresholds
for eviction.

```lua
require('vimficiency').setup({
  KEY_SESSION_CAPACITY  = 500,   -- keep more sessions
  MAX_RETENTION_SECONDS = 300,   -- keep a longer time window
})
```

Memory cost scales with the number of retained sessions × typical edit
span. Still negligible at defaults.

If `end Ns` asks for a window older than the ring retains, the alias
fails to resolve and you get "No recall session found within 'Ns'…" —
raise either cap.

## Recording cost

The ring only does O(1) work per keystroke (buffer append, timestamp
stamp, occasional eviction). The optimizer never runs until you call
`end N` or `end Ns`. Leave recall on all day without worry.

## Recall results are transient

A finished recall result lives only as long as its slot in the ring —
once the ring rotates or `recall off` runs, the result is gone. If you
want to keep one, promote it to disk before it ages out:

```vim
:Vimfy end 3s
:Vimfy save @ as nested-refactor
```

`@` is the "last finished session" shortcut. Without it you'd chase the
moving alias (`3s` a moment later isn't the same window).

## Turning it on

```vim
:Vimfy recall on        " Start recording
:Vimfy recall off       " Stop, discard ring
:Vimfy recall toggle
```

Or bind one of the `<Plug>Vimfy Recall*` maps — see
[7. Keymaps](07-keymaps.md).

## See also

- [5. Auto-suggest](05-auto-suggest.md) — surface results automatically
  without calling `end`.
- [6. Inspecting results](06-results.md) — what the output looks like
  and how to replay it.
- [7. Binding keys](07-keymaps.md) — useful to bind `<Plug>VimfyEnd3`
  / `<Plug>VimfyEnd3s` for frequently-used windows.

---

**[← Manual](03-manual.md)** | **[Index](./README.md)** | **[Next: Auto-suggest →](05-auto-suggest.md)**
