---
title: "Vimficiency User Guide"
---

# Vimficiency User Guide

Vimficiency watches how you edit and tells you shorter keystroke sequences
that would have produced the same result. You keep editing Vim the way you
already do — it just surfaces better motions in the background and lets you
replay them side-by-side to learn.

This guide covers everything you need to use the plugin day-to-day. For
internals and implementation notes, see `dev/`.

---

## Table of contents

1. [Installation](01-installation.md)
2. [The core idea: a session](02-sessions.md) — the 2×2 of session types
3. [Mark](03-mark.md) — manual start, manual end (alphabetical aliases)
4. [Watch](04-watch.md) — manual start, auto end (idle-fires `end`)
5. [Recall](05-recall.md) — auto start, manual end (`recall N` / `recall Ns`)
6. [Suggest](06-suggest.md) — auto start, auto end (idle-surfaces result)
7. [Inspecting results](07-results.md) — notifications, simulate, list, view
   - [7a. Session storage](07a-session-storage.md) — workspace (memory) vs. archive (disk)
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
" Recall is always on — just start editing.
" ... edit something ...
:Vimfy recall 6          " analyze the last 6 keys (or 'recall 3s' for time)
:Vimfy sim 6             " animate the suggestion
```

For anything else, start at [1. Installation](01-installation.md).

---

## One rule that runs through every page: the keymap contract

Vimficiency counts the keys you type. Any key you bind to invoke a
Vimfy action must therefore announce itself as admin activity —
otherwise the invoking keystroke is counted as motion and skews your
own results. Routing options, in order of ergonomics:

- `vimfy.map('n', '<leader>vs', 'start a')` — recommended
- `<Plug>VimfyStartA` — for users who prefer the Vim convention
- `require('vimficiency').wrap(fn)` — for autocommands / UI callbacks

A raw `nnoremap X :Vimfy start a<CR>` does **not** route correctly; the
`X` keypress is delivered before the mapping resolves. Full detail:
[8. Keymaps](08-keymaps.md). Later pages use these mappings freely
without re-explaining the rule — come back here if a binding surprises
you.
