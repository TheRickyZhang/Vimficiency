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
2. [The core idea: a session](02-sessions.md)
3. [Manual sessions](03-manual.md) — precise control, aliases `a`–`e`
4. [Recall](04-recall.md) — retrospective: `end 6` (keys) or `end 3s` (seconds)
5. [Auto-suggest](05-auto-suggest.md) — idle trigger today; keys/cost planned
6. [Inspecting results](06-results.md) — notifications, simulate, list, view
7. [Binding keys to Vimfy](07-keymaps.md) — `<Plug>` maps and `wrap()`
8. [Configuration](08-configuration.md) — setup knobs
9. [The effort model](09-effort-model.md) — how "cost" is computed
10. [Commands reference](10-commands.md) — cheat sheet
11. [Recommended workflows](11-workflows.md)
12. [Known limitations](12-limitations.md)
13. [Troubleshooting](13-troubleshooting.md)
14. [FAQ](14-faq.md)

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
