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

- `(0,0) -> (2,2)` — cursor moved from row 0 col 0 to row 2 col 2.
- `user:` — what you typed, with its effort cost in parentheses.
- Numbered lines — the top optimal sequences, sorted by cost ascending.

Cost is a keyboard-effort score (lower is better), not a keystroke count.
See [10. The effort model](10-effort-model.md) for how it's computed and
how to tune it to your layout.

## Simulating

`:Vimfy sim <alias> [count] [delay_ms]` opens a new tab with side-by-side
windows and animates:

- Your sequence (leftmost)
- The top suggestions (one per additional window)

**Keys inside the simulation tab:**
- `q` closes the simulation.

**Arguments:**
- `count` — how many optimal sequences to show (default: all saved)
- `delay_ms` — step delay in ms (default: 1000)

Example: `:Vimfy sim 6 2 300` shows your sequence + top 2 suggestions at 300ms
per step.

## Listing what's around

```vim
:Vimfy list
```

Shows currently active sessions and saved-to-disk results.

## Saving a result

`:Vimfy end <alias>` finishes and displays but doesn't write to disk. To
keep a result around, use `save`:

```vim
:Vimfy save <selector> [<name>]
```

The selector is a session alias (`a`, `3s`, `5`) or `@` for the most
recently finished session — the latter is the usual choice right after
`end`. The name is optional; omit it to reuse the selector as the
filename. For `@`, the default is the alias the last `:Vimfy end`
used.

```vim
:Vimfy end a
:Vimfy save @                   " writes saved/a.json
:Vimfy save @ my-refactor       " explicit name overrides the default
```

## Viewing a saved result

```vim
:Vimfy view my-refactor
```

Reads back a result previously saved via `:Vimfy save`. Tab-complete
lists what's available.

---

**[← Suggest](06-suggest.md)** | **[Index](./README.md)** | **[Next: Keymaps →](08-keymaps.md)**
