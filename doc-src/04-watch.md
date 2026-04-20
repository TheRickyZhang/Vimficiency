---
title: "Watch (manual start, auto end)"
---

# Watch (manual start, auto end)

Use this when you know the starting point but don't want to remember to
call `:Vimfy end` — the end fires on its own after an idle pause.

```vim
:Vimfy watch a       " Mark the start
" ... edit normally, then pause for `watch.idle.ms` ms ...
" Vimfy finishes automatically and shows the result.
```

Watch must be configured before use:

```lua
require('vimficiency').setup({
    watch = {
        idle = { ms = 3000 },
        cooldown_ms = 5000,
    },
})
```

Watch currently supports the `idle` trigger only. The `keys` and
`cost` triggers [shipped for Suggest](06-suggest.md#triggers) don't
yet apply here — the manual-start side of Watch doesn't benefit from
keystroke-count or cost-ratio firing the way Suggest's auto-start side
does. Ask if you'd use them and we can extend.

- Configuration parameters: see [Idle end-detection](09-configuration.md#idle-end-detection)
- Aliases follow the Mark grammar (letters only). `:Vimfy close a` aborts without optimizing.
- Re-issuing `:Vimfy watch a` on an active alias overwrites — the old timer is cancelled cleanly.
- The Mark auto-drop guards (idle / drift) apply here too; see [Mark](03-mark.md).

## See also

- [8. Binding keys](08-keymaps.md) — `<Plug>VimfyWatchA` / `<Plug>VimfyWatchB`.
- [9. Configuration](09-configuration.md) — full shape of the `watch` key in `setup{}`.
