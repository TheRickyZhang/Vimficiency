---
title: "FAQ"
---

# FAQ

> **Status: placeholder.** This page will accumulate common questions as
> they come up. Below are a few anticipated ones with short answers — full
> versions will be fleshed out over time.

## Does Vimficiency slow down Neovim?

No. Key capture is a small Lua append per keystroke. The optimizer only
runs on `finish` (or an auto-trigger), in C++, over a snapshot. Recall is
always on and the per-key overhead stays flat.

## Can I use it with Colemak / Dvorak / custom layouts?

The effort model's `keys[]` and `weights` knobs are designed for this,
but there isn't yet a "drop-in layout" file. Override the relevant entries
manually in `setup{}` — see [10. Effort model](10-effort-model.md).

## Does it work in visual or operator-pending mode?

Sessions span whatever modes you're in — the optimizer reasons about the
start-to-end delta, not mode transitions. Some mode-sensitive motions
are simplified; see [13. Limitations](13-limitations.md).

## Why didn't the "better" sequence actually get suggested?

A few common reasons:
- The optimizer's search budget truncated before reaching it.
- A motion it would have used isn't modeled (text-object final char,
  `gj`/`gk`, certain plugin mappings).
- Your `shiftwidth` is different from the one the optimizer is configured
  with, so indent-aware motions score differently.

See [14. Troubleshooting](14-troubleshooting.md).

## Can I export a result as something other than JSON?

Not today. `:Vimfy save @ my-name` writes JSON under
`stdpath('data')/vimficiency/saved/`; third-party tools can consume that
directly.

## How do I remove a saved result?

```vim
:Vimfy rm <name>
```

Tab-completes over saved names. Or delete the file directly from
`stdpath('data')/vimficiency/saved/`.

## Planned additions to this FAQ

- Comparison to alternatives (vim-golf scoring, key-logger-based tools)
- Known integration notes with `which-key`, `legendary`, `lazy.nvim`
- How to share a result with a colleague or team
