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

`:Vimfy sim <alias> [count]` opens a new tab with side-by-side windows and
animates your sequence (leftmost) and the top suggestions (one per additional
window)
- `count` — how many optimal sequences to show (default: all saved)

Replay opens paused after a brief precompute phase. Step forward with
`<Right>`, back with `<Left>`, and toggle auto-play with `<CR>`.
Manual stepping stops auto-play.

Each replay window shows a three-line virtual header above the buffer:

- progress through the global and local step counts
- current simulated mode (`NORMAL`, `INSERT`, `VISUAL`)
- the replay sequence itself, wrapped to the window width, with the current token highlighted

The cursor highlight also changes with the simulated mode, so insert and
visual segments are visible even in unfocused windows.

For a closer look at one buffer, `:Vimfy focus <N>` collapses the tab to just
the Nth window; `:Vimfy escape` restores the side-by-side layout. Close the
replay with `q`.

## Listing what's around

```vim
:Vimfy list
```

Shows currently active sessions and saved-to-disk results.

## Saving, storing, fetching

Every finished session starts in **session memory** (workspace) and
stays there until it rotates out. Copy it to disk with
`:Vimfy save`, move it with `:Vimfy store`, bring it back later with
`:Vimfy fetch`. `:Vimfy sim <name>` works against both — it replays
from memory first, falling back to disk.

See [7a. Session storage](07a-session-storage.md) for the full model,
collision rules, and worked examples.

## Viewing a saved result

```vim
:Vimfy view my-refactor
```

Reads back a saved result textually (without replaying). Tab-complete
on `:Vimfy view` lists what's available on disk.

---

**[← Suggest](06-suggest.md)** | **[Index](./README.md)** | **[Next: Keymaps →](08-keymaps.md)**
