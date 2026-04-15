**[← Sessions](02-sessions.md)** | **[Index](./README.md)** | **[Next: Watch →](04-watch.md)**

---

# 3. Mark (manual start, manual end)

Use these when you want precision analyzing a specific sequence or know *in advance* the entire sequence you want optimized.

```vim
:Vimfy start a       " Mark the start
" ... edit normally, any number of keystrokes ...
:Vimfy end a         " Mark the end, show suggestions
```

Marks are similar semantics to marks in vim, where start/end is similar to creating a mark and jumping back to it. However, our aliases can be any combination of letters, ex:  `a`, `refactor`, `WIP`.
- Up to 5 Mark sessions can be active concurrently.
- Starting a session at an already-used alias overwrites it.
- Sessions left idle for 5 minutes, or whose parameters would otherwise result in an invalid optimize query, are dropped automatically.
- Alternatively, you can call ```vim :Vimfy close a ``` to drop the session manually.

## See also

- [7. Inspecting results](07-results.md) — how to read the output and replay
  the suggestions.
- [8. Binding keys](08-keymaps.md) — use `<Plug>VimfyStartA` /
  `<Plug>VimfyEndA` so your session markers don't need typing `:Vimfy ...`.

---

**[← Sessions](02-sessions.md)** | **[Index](./README.md)** | **[Next: Watch →](04-watch.md)**
