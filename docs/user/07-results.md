**[← Suggest](06-suggest.md)** | **[Index](./README.md)** | **[Next: Keymaps →](08-keymaps.md)**

---

# 7. Inspecting results

## Reading the notification

When a session completes, you get something like:

```
vimficiency finished [a] (0,0) -> (2,2)
  user: cw xxx <Esc> jj ci yyy <Esc> (15.00)
  1. 3rx <C-d> ciw yyy <Esc> (8.00)
  2. 3rx w <C-d> 3ry (11.00)
  3. 3rx Wlj ciw yyy <Esc> (13.00)
```

- `(0,0) -> (2,2)` — cursor moved from row 0 col 0 to row 2 col 2 (0-indexed)
- `user:` — what you typed for reference
- Numbered lines — the top optimal sequences in order of cost.
- The decimal value in parentheses is the calculated effort for the corresponding sequence

Cost is a keyboard-effort score (lower is better), not a keystroke count.
See [10. The effort model](10-effort-model.md) for how it's computed and
how to tune it to your layout.

## Simulating

`:Vimfy sim <alias> [count] [delay_ms]` opens a new tab with side-by-side
windows and animates your sequence (leftmost) and the top suggestions (one per additional window)
- `count` — how many optimal sequences to show (default: all saved)
- `delay_ms` — step delay in ms (default: 1000)

You can close the simulation window with `q`

## Listing what's around

```vim
:Vimfy list
```

Shows currently active sessions and saved-to-disk results.

## Saving a result

`save` works for any finished session (Mark, Watch, Recall, Suggest).
`end` displays a result but doesn't touch disk:

```vim
:Vimfy save <selector> [<name>]
```

- Selectors: a session alias (`a`, `3s`, `5`) or `@` for the most recent finish.
- Name is optional — defaults to the selector; for `@` it reuses the alias the last `:Vimfy end` used.

```vim
:Vimfy end a
:Vimfy save @                   " writes saved/a.json
:Vimfy save @ my-refactor       " explicit name
:Vimfy save 3s                  " recall works the same way
```

Saved files live under `stdpath('data')/vimficiency/saved/`.

### Session handles vs. saved names

Separate namespaces. A session handle (`a`, `3s`, `5`) is in memory —
Marks last until overwritten/closed, Recall rotates out of the ring. A
saved name is a file on disk, durable across restarts. The same text
is allowed in both.

For Recall/Suggest, save promptly: `:Vimfy save @ <name>` right after
`end`, before the slice rotates out.

## Viewing a saved result

```vim
:Vimfy view my-refactor
```

Reads back a saved result. Tab-complete on `:Vimfy view` lists what's available.

---

**[← Suggest](06-suggest.md)** | **[Index](./README.md)** | **[Next: Keymaps →](08-keymaps.md)**
