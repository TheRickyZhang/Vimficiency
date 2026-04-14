# Vimficiency User Guide

Vimficiency watches how you edit and tells you shorter keystroke sequences
that would have produced the same result. You keep editing Vim the way you
already do — it just surfaces better motions in the background and lets you
replay them side-by-side to learn.

This guide covers everything you need to use the plugin day-to-day. For
internals and implementation notes, see the rest of `docs/`.

---

## Table of contents

1. [Installation](01-installation.md)
2. [The core idea: a session](02-sessions.md) — the 2×2 of session types
3. [Mark](03-mark.md) — manual start, manual end (alphabetical aliases)
4. [Watch](04-watch.md) — manual start, auto end (idle-fires `end`)
5. [Recall](05-recall.md) — auto start, manual end (`end N` / `end Ns`)
6. [Suggest](06-suggest.md) — auto start, auto end (idle-surfaces result)
7. [Inspecting results](07-results.md) — notifications, simulate, list, view
8. [Binding keys to Vimfy](08-keymaps.md) — `<Plug>` maps and `wrap()`
9. [Configuration](09-configuration.md) — setup knobs
10. [The effort model](10-effort-model.md) — how "cost" is computed
11. [Commands reference](11-commands.md) — cheat sheet
12. [Recommended workflows](12-workflows.md)
13. [Known limitations](13-limitations.md)
14. [Troubleshooting](14-troubleshooting.md)
15. [FAQ](15-faq.md)

---

## Quickstart

```lua
-- In your Neovim config:
require('vimficiency').setup()
```

```vim
:Vimfy recall on         " turn on rolling capture
" ... edit something ...
:Vimfy end 6             " analyze the last 6 keys (or 'end 3s' for time)
:Vimfy sim 6             " animate the suggestion
```

For anything else, start at [1. Installation](01-installation.md).
