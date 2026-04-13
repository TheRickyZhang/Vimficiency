**[← Sessions](02-sessions.md)** | **[Index](./README.md)** | **[Next: Recall →](04-recall.md)**

---

# 3. Manual sessions (precise control)

Use these when you know *in advance* that you're about to do an edit worth
analyzing, or when you want clean start/end boundaries.

```vim
:Vimfy start a       " Mark the start
" ... edit normally, any number of keystrokes ...
:Vimfy end a         " Mark the end, show suggestions
```

Aliases are alphabetic names. The recommended convention is single letters
`a` through `e` — short to type at session boundaries, and there's a
capacity of 5 concurrent manual sessions. Longer names (`refactor`,
`wip`) also work but aren't the typical ergonomic path. Digit-only names
and names ending in `s` are reserved for [recall](04-recall.md).

Starting a session at an already-used alias overwrites it.

To throw away an in-progress session without running the optimizer:

```vim
:Vimfy close a
```

## Saving to disk

`end` finishes the session but doesn't touch disk. To keep the result,
run `save` next — either by alias or via the `@` shortcut for the most
recent finish:

```vim
:Vimfy end a
:Vimfy save a as my-refactor
" or equivalently, right after `end`:
:Vimfy save @ as my-refactor
" ... later, or in another Neovim instance ...
:Vimfy view my-refactor
```

The `as` keyword is optional — `:Vimfy save a my-refactor` works too,
but `as` reads better in scripts.

Saved files live under `stdpath('data')/vimficiency/saved/`. Tab-complete on
`:Vimfy view` shows what's available.

### Manual handles vs. saved names

These are **separate namespaces**: a manual handle lives in memory until
you overwrite or close it, while a saved name is a file on disk. The
same text is allowed in both — `:Vimfy save a as a` is valid if you
want.

## See also

- [6. Inspecting results](06-results.md) — how to read the output and replay
  the suggestions.
- [7. Binding keys](07-keymaps.md) — use `<Plug>VimfyStartA` /
  `<Plug>VimfyEndA` so your session markers don't need typing `:Vimfy ...`.

---

**[← Sessions](02-sessions.md)** | **[Index](./README.md)** | **[Next: Recall →](04-recall.md)**
