**[← Mark](03-mark.md)** | **[Index](./README.md)** | **[Next: Recall →](05-recall.md)**

---

# 4. Watch (manual start, auto end)

Watch is Mark's "fire and forget" cousin. You **mark the start** the
same way — precise boundary, your choice of alphabetic alias — but the
**end is triggered automatically** after a configurable idle pause.
Useful when you know the starting point but don't want to remember to
call `:Vimfy end`.

```vim
:Vimfy watch a        " Mark the start
" ... edit normally ...
" ... pause typing for `watch.idle_ms` milliseconds ...
" Vimfy finishes automatically; the result notification appears.
```

Watch must be configured before you use it:

```lua
require('vimficiency').setup({
    watch = { idle_ms = 3000, cooldown_ms = 5000 },
})
```

- `idle_ms` — how long to wait after your last real keystroke before
  auto-ending.
- `cooldown_ms` — minimum time between two consecutive auto-fires. Acts
  as a safety rail if you configure a very low `idle_ms`.

Running `:Vimfy watch a` without the `watch = {...}` block in `setup{}`
is an error; you'll get a one-line notification pointing you at the
config.

Aliases follow the same grammar as Mark (letters only), and `:Vimfy
close a` works identically to abort without running the optimizer.

Re-issuing `:Vimfy watch <alias>` on an active alias overwrites the
existing session — the old idle timer is cancelled cleanly and a new
one is armed. The usual 5-minute idle / 500-line drift guards from
[Mark](03-mark.md) apply here too: a forgotten watch can't keep a
key subscriber alive indefinitely.

## When to reach for Watch vs. the alternatives

- **[Mark](03-mark.md)** — both boundaries are yours. Pick this when
  you want explicit control over when analysis runs.
- **Watch** (this page) — start is yours, end is automatic. Pick this
  when the starting point is obvious but you don't want to babysit the
  end.
- **[Recall](05-recall.md)** — no pre-planning at all. Analyze what
  you just did, measured backwards by key count or seconds.
- **[Suggest](06-suggest.md)** — fully automatic on both ends. Pick
  this to learn passively while editing normally.

## Interaction with auto-suggest

Watch and [Suggest](06-suggest.md) share the same idle-detection
engine but are **independently configurable**. You can run both at
once with different `idle_ms` thresholds; each owns its own timer and
cooldown. Turning one on does not imply the other.

## See also

- [8. Binding keys](08-keymaps.md) — `<Plug>VimfyWatchA` / `<Plug>VimfyWatchB`
  are registered alongside the usual `Start` / `End` maps.
- [9. Configuration](09-configuration.md) — full shape of the `watch`
  key in `setup{}`.

---

**[← Mark](03-mark.md)** | **[Index](./README.md)** | **[Next: Recall →](05-recall.md)**
