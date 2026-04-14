**[← Sessions](02-sessions.md)** | **[Index](./README.md)** | **[Next: Watch →](04-watch.md)**

---

# 3. Mark (manual start, manual end)

Use these when you know *in advance* that you're about to do an edit worth
analyzing, or when you want clean start/end boundaries.

```vim
:Vimfy start a       " Mark the start
" ... edit normally, any number of keystrokes ...
:Vimfy end a         " Mark the end, show suggestions
```

> The command verb is still `:Vimfy start` / `:Vimfy end` — the *type* is
> called **Mark** to keep the taxonomy crisp (see [2. Sessions](02-sessions.md)).
> `:Vimfy start` doesn't change meaning.

Aliases are **letters only** — `a`, `refactor`, `WIP`, mixed case all
fine. No digits, hyphens, underscores, or other punctuation. Up to 5
Mark sessions can be active concurrently. Digit-only names (`42`)
and names ending in `s` (`3s`) are reserved for [recall](05-recall.md);
any other shape (e.g. `my-refactor`, `v2`, `_tmp`) is rejected.

Starting a session at an already-used alias overwrites it.

To throw away an in-progress session without running the optimizer:

```vim
:Vimfy close a
```

Sessions left idle for 5 minutes, or whose cursor drifts more than 500
lines from the start row (the optimizer's analysis ceiling), are dropped
automatically with a one-line notice on the next keypress. Start a
fresh session after — the alias is freed.

If you'd rather not remember to call `:Vimfy end` at all, see
[4. Watch](04-watch.md) — same alphabetic aliases, same `:Vimfy start`-
style anchoring, but the end is triggered by idle time instead.

## Saving to disk

`end` finishes the session but doesn't touch disk. To keep the result,
run `save` next — either by alias or via the `@` shortcut for the most
recent finish:

```vim
:Vimfy end a
:Vimfy save a my-refactor
" or equivalently, right after `end`:
:Vimfy save @ my-refactor
" ... later, or in another Neovim instance ...
:Vimfy view my-refactor
```

The name is optional. Omit it and the selector is reused as the
filename:

```vim
:Vimfy save a            " writes saved/a.json
:Vimfy save 3s           " writes saved/3s.json
:Vimfy save @            " reuses the alias passed to the last `:Vimfy end`
```

Saved files live under `stdpath('data')/vimficiency/saved/`. Tab-complete on
`:Vimfy view` shows what's available.

### Mark handles vs. saved names

These are **separate namespaces**: a Mark handle lives in memory until
you overwrite or close it, while a saved name is a file on disk. The
same text is allowed in both — `:Vimfy save a` (reusing `a` as the
filename) is the common path.

## See also

- [7. Inspecting results](07-results.md) — how to read the output and replay
  the suggestions.
- [8. Binding keys](08-keymaps.md) — use `<Plug>VimfyStartA` /
  `<Plug>VimfyEndA` so your session markers don't need typing `:Vimfy ...`.

---

**[← Sessions](02-sessions.md)** | **[Index](./README.md)** | **[Next: Watch →](04-watch.md)**
